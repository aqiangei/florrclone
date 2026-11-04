#include "test.h"
#include "test_data.h"
#include "client/ui/menus.h"
#include "server/systems/combat.h"
#include "server/systems/dungeons.h"
#include "server/systems/petals.h"
#include "server/systems/movement.h"
#include "server/systems/spawning.h"
#include "server/systems/npcs.h"
#include "server/systems/loot.h"
#include "server/loot_eligibility.h"
#include "shared/game/config.h"
#include "shared/game/shop.h"
#include "client/camera.h"
using namespace flix;
TEST(feedback_departed_top_contributor_does_not_erase_remaining_split_owner_loot){
    const auto& c=testsupport::shippedContent();World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{82};LootSystem loot;
    const auto gone=w.create();w.add<PlayerTag>(gone);w.destroy(gone);
    const auto living=w.create();w.add<PlayerTag>(living);w.add<Transform>(living,Transform{{10000,10000},0});w.add<Health>(living,Health{100,100});
    const auto corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex("sandstorm"),Rarity::Super,1});
    w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100});
    Bounty tally;tally.credit(gone,90,41);tally.credit(living,10,42);w.add<Bounty>(corpse,tally);w.add<Dead>(corpse,Dead{gone});
    loot.run(w,grid,c,rng,0,0,commands,events);commands.flush();int drops=0;
    Query<DropTag,DropItem> all{w};all.each([&](Entity,DropTag&,DropItem& d){if(LootSystem::mayPickUp(d,living,42))drops+=d.quantity;});CHECK(drops>0);
}
TEST(feedback_hyper_loot_accepts_a_single_effective_hit_below_one_percent){
    const auto& c=testsupport::shippedContent();World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{29};LootSystem loot;
    const auto player=w.create();w.add<PlayerTag>(player);w.add<Transform>(player,Transform{{10000,10000},0});w.add<Health>(player,Health{100,100});
    const auto corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex("sandstorm"),Rarity::Apex,1});
    w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,1e12});Bounty tally;tally.credit(player,1);w.add<Bounty>(corpse,tally);w.add<Dead>(corpse,Dead{player});
    loot.run(w,grid,c,rng,0,0,commands,events);commands.flush();int count=0;Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& d){count+=d.quantity;});CHECK(count>0);
}
TEST(feedback_dummy_total_preserves_old_hits_and_excludes_other_players){
    const auto& c=testsupport::shippedContent();WorldRenderer renderer;renderer.setContent(&c);WorldView view;
    RemoteEntity dummy;dummy.netId=123;dummy.kind=net::EntityKind::Npc;dummy.typeIndex=c.mobIndex("target_dummy");view.seedForTest(dummy);
    ViewEvent hit;hit.kind=net::EventKind::Damage;hit.netId=123;hit.amount=50;hit.flag=net::DamageByViewer;
    view.events().push_back(hit);renderer.ingestEvents(view);renderer.update(20);
    hit.flag=0;hit.amount=999;view.events().push_back(hit);renderer.ingestEvents(view);
    hit.flag=net::DamageByViewer;hit.amount=25;view.events().push_back(hit);renderer.ingestEvents(view);
    CHECK_NEAR(renderer.dummyTotalDamage(123),75,1e-8);
    hit.kind=net::EventKind::Killed;view.events().push_back(hit);renderer.ingestEvents(view);CHECK_NEAR(renderer.dummyTotalDamage(123),0,1e-8);
}
TEST(feedback_ant_holes_use_lower_tier_soldiers_and_one_same_tier_queen_below_half){
    const auto& c=testsupport::shippedContent();
    for(int tier=0;tier<kLadderRarityCount;++tier){World w;CommandBuffer commands{w};SpawnSystem spawn;Terrain terrain;Rng rng{71};
        const auto rarity=static_cast<Rarity>(tier);const auto nest=spawn.spawnMob(w,terrain,c,c.mobIndex("ant_hole"),rarity,{1000,1000},Realm::Overworld,0,rng);
        CHECK(nest!=NULL_ENTITY);if(nest==NULL_ENTITY)continue;
        auto inspect=[&](){int queens=0;Query<HoleTether,MobType> children{w};children.each([&](Entity,HoleTether& link,MobType& type){
            if(link.hole!=nest)return;const bool queen=c.mob(type.configIndex).id=="queen_ant";
            if(queen)++queens;CHECK(type.rarity==(queen?rarity:downgradeRarity(rarity)));
        });return queens;};
        CHECK_EQ(inspect(),0);auto step=[&](double at){spawn.run(w,terrain,c,std::vector<RealmPoint>{{{1000,1000},Realm::Overworld}},rng,at,net::kTickSeconds,commands);commands.flush();};
        w.get<Health>(nest).current=w.get<Health>(nest).max*.51;step(1);CHECK_EQ(inspect(),0);
        w.get<Health>(nest).current=w.get<Health>(nest).max*.49;step(34);CHECK_EQ(inspect(),1);
        step(1000);CHECK_EQ(inspect(),1);
    }
}
TEST(feedback_ant_hole_wave_retries_when_the_live_cap_has_room_again){
    const auto& c=testsupport::shippedContent();World w;CommandBuffer commands{w};SpawnSystem spawn;Terrain terrain;Rng rng{11};
    const auto nest=spawn.spawnMob(w,terrain,c,c.mobIndex("ant_hole"),Rarity::Super,{1000,1000},Realm::Overworld,0,rng);
    spawn.mobCap=7;w.get<Health>(nest).current*=.99;
    auto step=[&](double now){spawn.run(w,terrain,c,std::vector<RealmPoint>{{{1000,1000},Realm::Overworld}},rng,now,net::kTickSeconds,commands);commands.flush();};
    step(1);CHECK(!w.get<NestWaves>(nest).pending.empty());spawn.mobCap=40;step(34);
    CHECK(w.get<NestWaves>(nest).pending.empty());CHECK(!w.get<NestWaves>(nest).children.empty());
}
TEST(feedback_one_shot_super_and_higher_ant_holes_still_release_their_brood){
    const auto& c=testsupport::shippedContent();
    for(const auto rarity:{Rarity::Super,Rarity::Unique,Rarity::Apex}){
        World w;CommandBuffer commands{w};SpawnSystem spawn;Terrain terrain;Rng rng{29};
        const auto nest=spawn.spawnMob(w,terrain,c,c.mobIndex("ant_hole"),rarity,{1000,1000},Realm::Overworld,0,rng);
        w.get<Health>(nest).current=0;w.add<Dead>(nest);
        spawn.run(w,terrain,c,std::vector<RealmPoint>{{{1000,1000},Realm::Overworld}},rng,1,net::kTickSeconds,commands);commands.flush();
        int queens=0,ants=0;Query<HoleTether,MobType> children{w};children.each([&](Entity,HoleTether& link,MobType& type){
            if(link.hole!=nest)return;if(c.mob(type.configIndex).id=="queen_ant"){++queens;CHECK(type.rarity==rarity);}else{++ants;CHECK(type.rarity==downgradeRarity(rarity));}
        });CHECK_EQ(queens,1);CHECK(ants>6);
    }
}
TEST(feedback_computer_is_a_separate_map_with_art_spawn_bands_and_assembler){
    WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    bool found=false;const auto realm=maps.realmOfId("cyberspace",found);CHECK(found);if(!found)return;
    const auto* map=maps.forRealm(realm);CHECK(map!=nullptr);if(!map)return;
    CHECK_EQ(map->displayName(),std::string("Computer"));CHECK_EQ(map->width(),108);CHECK_EQ(map->height(),108);
    CHECK(map->usesReferenceSpawnCurve());CHECK(!map->playerSpawns().empty());CHECK(!map->artFiles().empty());
    CHECK(!terrain.blocked({25984,2240},realm));
    const auto factory=maps.realmOfId("computer",found);CHECK(found);CHECK(factory!=realm);
    CHECK_EQ(maps.forRealm(factory)->displayName(),std::string("Factory"));
    const auto& c=testsupport::shippedContent();const auto assembler=c.mobIndex("cyber_assembler");CHECK(assembler!=kInvalidIndex);
    const auto& cyber=c.mobGroup(c.mobGroupIndex("cyberspace"));CHECK_EQ(cyber.members.size(),std::size_t(8));
    for(const auto& member:cyber.members){const auto& id=c.mob(member.mob).id;
        CHECK(id=="javascript"||id=="c++"||id=="dust"||id=="glitch"||id=="glitch_flower"||id=="cube"||id=="sun"||id=="server");}
    if(assembler!=kInvalidIndex){CHECK(c.mob(assembler).npc.still);CHECK(c.mob(assembler).npc.recipes.size()>=10);}
    NpcSystem npcs;std::vector<std::string> warnings;npcs.loadSites(maps,c,warnings);World world;npcs.placeMissing(world,terrain,c,0);
    int converters=0;Query<Npc,Transform> services{world};services.each([&](Entity e,Npc& npc,Transform& at){
        if(at.realm!=realm||npc.configIndex!=assembler)return;++converters;
        CHECK(!terrain.blocked(at.position,realm));CHECK(npc.recipe<c.mob(assembler).npc.recipes.size());
        const auto attacker=world.create();world.add<PlayerTag>(attacker);world.add<Faction>(attacker,Faction{Team::Players,false});
        CombatSystem combat;CHECK(combat.applyDamage(world,e,attacker,1e20,0).refused);
    });CHECK_EQ(converters,kLadderRarityCount);
    for(const char* id:{"javascript","c++","bomb","glitch","ram","cursor","computer_chip","cube","icosahedron","spaceship","capacitor","ruby"})
        CHECK(c.petalIndex(id)!=kInvalidIndex);
}
TEST(feedback_splitter_rewards_keep_the_contributing_body_and_refresh_only_after_removal) {
    World world;
    const Entity left=world.create(),right=world.create();
    for(const Entity body:{left,right}) {
        world.add<PlayerTag>(body);
        world.add<PlayerAccount>(body,PlayerAccount{"same-account","test",17,false});
    }
    Bounty bounty;bounty.credit(left,10,17);bounty.credit(right,20,17);
    CHECK_EQ(bounty.contributors.size(),std::size_t(1));
    const auto share=refreshedShare(world,bounty.contributors.front());
    CHECK(share.player==right);CHECK_NEAR(share.damage,30,1e-12);
    world.add<Health>(right,Health{0,100});
    CHECK(liveContributorBody(world,share)==left);
    world.destroy(right);
    CHECK(liveContributorBody(world,share)==left);
}
TEST(feedback_both_sticks_are_unbreakable_in_shipped_content) {
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));
    for(const char* id:{"mysterious_stick","magic_stick"}) {
        const auto& config=content().petal(content().petalIndex(id));
        CHECK(!config.breakable);
    }
}
TEST(feedback_golden_leaf_duplicate_reduces_real_shared_reload_multiplicatively) {
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));
    const auto leaf=content().petalIndex("golden_leaf");
    EquippedPetal equipped;equipped.configIndex=leaf;equipped.rarity=Rarity::Super;
    auto at=[&](int i){return i==0?equipped:EquippedPetal{};};
    const double single=barReloadCut(content(),at);
    SkillSet skills;skills.set(SkillId::Loadout,4);skills.set(SkillId::Duplicator,2);
    CHECK_NEAR(barReloadCut(content(),at,&skills),1-(1-single)*(1-single),1e-12);
    skills.set(SkillId::Duplicator,1);
    CHECK_NEAR(barReloadCut(content(),at,&skills),single,1e-12);
    CHECK_EQ(content().petal(content().petalIndex("magic_stick")).petCount,2);
    CHECK_NEAR(content().petal(content().petalIndex("magic_stick")).petLifetimeMillis,20000,1e-9);
}
TEST(feedback_public_guild_tags_follow_roster_and_zoom_reaches_quarter_scale) {
    WorldView view;RemoteEntity player;player.netId=9;player.name="Visitor";
    player.kind=net::EntityKind::Player;
    view.setGuildNames({{"Visitor","TestGuild"}});view.seedForTest(player);
    CHECK_EQ(view.entities().at(9).guildName,std::string("TestGuild"));
    view.setGuildNames({{"Visitor","NewGuild"}});
    CHECK_EQ(view.entities().at(9).guildName,std::string("NewGuild"));
    view.setGuildNames({});CHECK(view.entities().at(9).guildName.empty());
    Camera camera;camera.userZoom=kMinZoom;
    CHECK_NEAR(camera.zoom(),0.25,1e-12);
    camera.loadoutZoom=0.5;CHECK_NEAR(camera.zoom(),0.125,1e-12);
}
namespace {
bool feedbackContent(){std::string error;return loadContent(testsupport::dataDir(),error);}
int antFamily(const std::string& id){return id.find("fire_ant")!=std::string::npos?1:id.find("termite")!=std::string::npos?2:id.find("ant")!=std::string::npos?0:-1;}
int familyOf(const MapElement& e){double weights[3]={};for(const auto& row:e.mobDistribution){const int f=antFamily(row.name);if(f>=0)weights[f]+=row.weight;}int best=-1;for(int n=0;n<3;++n)if(weights[n]>0&&(best<0||weights[n]>weights[best]))best=n;return best;}
}
TEST(feedback_corruption_and_pharaoh_are_worn_and_clear_on_unequip) {
    CHECK(feedbackContent());
    World w; CommandBuffer commands{w}; PetalSystem petals;
    const auto p=w.create();
    w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{10000,10000},0});
    w.add<Motion>(p);w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{1000,1000});
    w.add<Faction>(p,Faction{Team::Players,false});w.add<PlayerProgress>(p);
    w.add<PlayerInput>(p);w.add<PlayerModifiers>(p);w.add<Loadout>(p);
    w.add<PetalRing>(p);w.add<Afflictions>(p);w.add<HitCooldowns>(p);w.add<PlayerVisuals>(p);
    w.get<PlayerProgress>(p).level=100;
    auto& load=w.get<Loadout>(p);
    load.slots[0].configIndex=content().petalIndex("corruption");load.slots[0].rarity=Rarity::Unique;
    load.slots[1].configIndex=content().petalIndex("pharaoh_s_crown");load.slots[1].rarity=Rarity::Unique;
    petals.run(w,content(),1000,net::kTickSeconds,commands);commands.flush();
    CHECK(w.get<PlayerVisuals>(p).equippedCorruption);
    CHECK((w.get<PlayerVisuals>(p).equipFlags & EquipPharaohCrown)!=0);
    w.get<Loadout>(p).slots[2]=w.get<Loadout>(p).slots[1];
    for(int second=2;second<=14;++second){
        petals.run(w,content(),second*1000.0,net::kTickSeconds,commands);commands.flush();
    }
    int mummies=0;
    Query<Pet,MobType> summoned{w};
    summoned.each([&](Entity e,Pet& pet,MobType& type){
        if(pet.owner==p && !w.has<Dead>(e) && type.configIndex==content().mobIndex("mummy_beetle")){
            ++mummies;CHECK(type.rarity==Rarity::Mythic);CHECK(w.has<RisesUndead>(e));
        }
    });
    CHECK_EQ(mummies,6);
    const auto other=w.create();
    w.add<PlayerTag>(other);w.add<Faction>(other,Faction{Team::Players,false});
    w.add<PlayerProgress>(other);w.add<PlayerVisuals>(other);
    w.get<PlayerProgress>(other).level=20;
    CHECK(CombatSystem::canDamage(w,p,other));
    CHECK(CombatSystem::canDamage(w,other,p));
    w.get<PlayerProgress>(other).level=19;
    CHECK(!CombatSystem::canDamage(w,p,other));
    CHECK(!CombatSystem::canDamage(w,other,p));
    w.get<Loadout>(p).slots[0]=LoadoutSlot{};
    w.get<Loadout>(p).slots[1]=LoadoutSlot{};
    w.get<Loadout>(p).slots[2]=LoadoutSlot{};
    w.get<PlayerVisuals>(p).corrupted=true;
    petals.run(w,content(),15000,net::kTickSeconds,commands);commands.flush();
    CHECK(!w.get<PlayerVisuals>(p).equippedCorruption);
    CHECK((w.get<PlayerVisuals>(p).equipFlags & EquipPharaohCrown)==0);
    CHECK(w.get<PlayerVisuals>(p).corrupted);
    w.get<PlayerVisuals>(p).corrupted=false;
    w.get<PlayerProgress>(other).level=20;
    CHECK(!CombatSystem::canDamage(w,p,other));
}
TEST(feedback_talent_vision_changes_camera_and_does_not_stack_with_antennae) {
    CHECK(feedbackContent());Profile p;
    CHECK_NEAR(loadoutCameraZoom(p,content()),1,1e-9);
    p.skills.set(SkillId::Loadout,0);
    const double expected[]={.8,2.0/3.0,.5};
    for(int tier=0;tier<3;++tier){p.skills.set(SkillId::Antennae,tier);CHECK_NEAR(loadoutCameraZoom(p,content()),expected[tier],1e-9);}
    p.loadout.resize(kLoadoutSlots);p.loadout[0].petalIndex=content().petalIndex("antennae");p.loadout[0].rarity=Rarity::Super;
    CHECK_NEAR(loadoutCameraZoom(p,content()),std::min(.5,petalCameraZoom(content(),p.loadout[0].petalIndex,Rarity::Super)),1e-9);
}
TEST(feedback_shop_each_hour_has_exactly_two_super_offers_at_the_shared_price) {
    CHECK(feedbackContent());
    for (int rotation=0;rotation<200;++rotation) {
        const auto offers=shopOffers(rotation);int supers=0;
        CHECK_EQ(offers.size(),static_cast<std::size_t>(kShopOfferCount));
        for (const auto& offer:offers) {
            supers += offer.rarity==Rarity::Super;
            CHECK(shopSellsPetal(offer.petalIndex));
            CHECK(shopSellsRarity(offer.rarity));
            CHECK_NEAR(offer.price,std::floor(shopPrice(offer.petalIndex,offer.rarity)*(100-offer.discountPercent)/100.0),1e-9);
        }
        CHECK_EQ(supers,2);
        CHECK_EQ(shopRotationEnd(rotation)-shopRotationEnd(rotation-1),3600);
    }
}
TEST(feedback_scarce_shop_petals_keep_their_authored_price_premium) {
    CHECK(feedbackContent());
    const auto mimic=content().petalIndex("mimic");
    const auto basic=content().petalIndex("basic");
    const double price=shopPrice(mimic,Rarity::Super);
    CHECK_NEAR(price,6000,1e-9);CHECK(price>shopPrice(basic,Rarity::Super)*6);
    CHECK(!shopSellsRarity(Rarity::Unique));
    CHECK(shopPrice(mimic,Rarity::Ultra)>shopPrice(basic,Rarity::Ultra)*4);
}
TEST(feedback_talent_wiki_values_and_duplicator_eligibility) {
    SkillSet s;s.set(SkillId::Loadout,4);s.set(SkillId::Magnetism,0);CHECK_NEAR(s.magnetismBonus(),1000,1e-9);
    s.set(SkillId::Luck,8);CHECK_NEAR(s.luck(),2.7,1e-9);
    s.set(SkillId::Summoner,0);CHECK_NEAR(s.summonerScale(),1.15,1e-9);
    s.set(SkillId::Summoner,8);CHECK_NEAR(s.summonerScale(),4.0456,1e-9);
    s.set(SkillId::SharpEdges,5);s.set(SkillId::BodyToxicity,0);CHECK(!s.bodyToxicity());s.set(SkillId::SharpEdges,6);CHECK(s.bodyToxicity());
    s.set(SkillId::Duplicator,2);CHECK_EQ(s.petalCopies(1,0,7,true),2);CHECK_EQ(s.petalCopies(1,1,7,true),1);CHECK_EQ(s.petalCopies(1,0,8,true),1);CHECK_EQ(s.petalCopies(1,0,7,false),1);
    s.set(SkillId::Duplicator,3);CHECK_EQ(s.petalCopies(1,0,8,true),2);CHECK_EQ(s.petalCopies(4,1,8,true),6);
    s.set(SkillId::Reload,0);CHECK_NEAR(s.equipReloadFloorMillis(),2000,1e-9);s.set(SkillId::Reload,4);CHECK_NEAR(s.equipReloadFloorMillis(),0,1e-9);
}
TEST(feedback_double_tp_covers_existing_levels_and_keeps_spending_and_fixed_credit) {
    SkillSet skills;
    CHECK_EQ(availableTalentPoints(1,skills),2);
    CHECK_EQ(availableTalentPoints(100,skills),200);
    skills.set(SkillId::Health,2);
    CHECK_EQ(availableTalentPoints(100,skills,7),207-skills.spent());
    const int before=availableTalentPoints(100,skills,7);
    CHECK_EQ(availableTalentPoints(101,skills,7)-before,2);
    skillRefundTo(skills,SkillId::Health,-1);
    CHECK_EQ(availableTalentPoints(100,skills,7),207);
    Profile profile;profile.level=100;profile.talentPointBonus=7;
    CHECK_EQ(profile.talentPoints(),207);
}

TEST(feedback_petal_health_talent_updates_live_shared_and_independent_pools_without_reset) {
    CHECK(feedbackContent());
    for(const auto* id:{"basic","sand"}) {
        World w;CommandBuffer commands{w};PetalSystem petals;const auto player=w.create();
        w.add<PlayerTag>(player);w.add<Transform>(player,Transform{{10000,10000},0});
        w.add<Motion>(player);w.add<Body>(player,Body{25,1});w.add<Health>(player,Health{1000,1000});
        w.add<Faction>(player,Faction{Team::Players,false});w.add<PlayerProgress>(player);
        w.add<PlayerInput>(player);w.add<PlayerModifiers>(player);w.add<Loadout>(player);
        w.add<PetalRing>(player);w.add<Afflictions>(player);w.add<HitCooldowns>(player);w.add<PlayerSkillTree>(player);
        const auto index=content().petalIndex(id);auto& slot=w.get<Loadout>(player).slots[0];
        slot.configIndex=index;slot.rarity=Rarity::Super;
        double now=1000;
        auto tick=[&](int n){for(int i=0;i<n;++i){now+=net::kTickMillis;petals.run(w,content(),now,net::kTickSeconds,commands);commands.flush();}};
        auto live=[&](){std::vector<Entity> out;Query<PetalInstance,Health> q{w};q.each([&](Entity e,PetalInstance& p,Health&){if(p.owner==player&&p.slot==0)out.push_back(e);});return out;};
        tick(400);auto instances=live();CHECK(!instances.empty());if(instances.empty())continue;
        const Entity first=instances.front();const double base=w.get<Health>(first).max;CHECK(base>1);
        w.get<Health>(first).current=base*.5;tick(1);
        const double fraction=w.get<Health>(first).current/base;
        auto& skills=w.get<PlayerSkillTree>(player).skills;skills.set(SkillId::PetalHealth,8);tick(1);
        const double boosted=std::round(content().petalStats(index,Rarity::Super).health*skills.petalHealthScale());
        CHECK(w.isAlive(first));CHECK_NEAR(w.get<Health>(first).max,boosted,1e-8);
        CHECK_NEAR(w.get<Health>(first).current,boosted*fraction,1e-8);
        skills.set(SkillId::PetalHealth,-1);tick(1);
        CHECK_NEAR(w.get<Health>(first).max,base,1e-8);
        CHECK_NEAR(w.get<Health>(first).current,base*fraction,1e-8);
        // A talent change cannot revive broken petals or shorten their timers.
        for(const auto e:live())w.get<Health>(e).current=0;
        tick(1);CHECK(live().empty());const double deadline=w.get<Loadout>(player).slots[0].reloadReadyAtMillis;
        CHECK(deadline>now);skills.set(SkillId::PetalHealth,8);tick(1);
        CHECK(live().empty());CHECK_NEAR(w.get<Loadout>(player).slots[0].reloadReadyAtMillis,deadline,1e-8);
        tick(400);instances=live();CHECK(!instances.empty());
        for(const auto e:instances){CHECK_NEAR(w.get<Health>(e).max,boosted,1e-8);CHECK_NEAR(w.get<Health>(e).current,boosted,1e-8);}
    }
}
TEST(feedback_poison_and_concentrated_poison_affect_real_poison_status) {
    World w;const auto player=w.create(),victim=w.create();w.add<PlayerTag>(player);auto& tree=w.add<PlayerSkillTree>(player);
    tree.skills.set(SkillId::Poison,8);tree.skills.set(SkillId::ConcentratedPoison,0);w.add<Health>(victim,Health{1000,1000});
    CombatSystem combat;combat.applyPoison(w,victim,player,100,1200,1000);
    const auto& status=w.get<Afflictions>(victim);CHECK_NEAR(status.poisonPerSecond,187.5,1e-8);CHECK_NEAR(status.poisonUntilMillis,2000,1e-8);
}
TEST(feedback_static_holes_bush_and_tomb_resist_motion_and_separation) {
    CHECK(feedbackContent());World w;Terrain terrain;SpawnSystem spawns;Rng rng{91};MovementSystem movement;
    std::vector<std::pair<Entity,Vec2>> fixed;
    for(const auto* id:{"fire_ant_hole","ant_hole","bush","tomb"}){
        const auto index=content().mobIndex(id);CHECK(index!=kInvalidIndex);CHECK(content().mob(index).ai==AiKind::Stationary);
        Vec2 at{10000.0+fixed.size()*2000.0,10000};const auto e=spawns.spawnMob(w,terrain,content(),index,Rarity::Common,at,Realm::Overworld,0,rng);CHECK(e!=NULL_ENTITY);
        if(e==NULL_ENTITY)continue;fixed.push_back({e,w.get<Transform>(e).position});w.get<Motion>(e).velocity={500,0};w.ensure<Knockback>(e).impulse={800,600};
        const auto other=spawns.spawnMob(w,terrain,content(),content().mobIndex("ladybug"),Rarity::Common,at+Vec2{5,0},Realm::Overworld,0,rng);CHECK(other!=NULL_ENTITY);
    }
    for(int tick=0;tick<120;++tick)movement.run(w,terrain,tick*net::kTickMillis,net::kTickSeconds);
    for(const auto& [e,at]:fixed)CHECK_NEAR(distance(w.get<Transform>(e).position,at),0,1e-8);
}
TEST(feedback_mound_interiors_scale_by_rarity_with_safe_spawns) {
    CHECK(feedbackContent());WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    CHECK(maps.maps().size()<kMaxWorldMaps);Rng rng{121};
    for(int tier=0;tier<kRarityCount;++tier){const auto id=tier==0?std::string("termite_mound"):"termite_mound_t"+std::to_string(tier);
        const auto copies=maps.copiesOf(id);CHECK_EQ(copies.size(),8u);if(copies.empty())continue;
        const auto* map=maps.forRealm(copies.front());CHECK(map);if(!map)continue;CHECK_EQ(map->width(),16+2*tier);
        CHECK_NEAR(terrain.realmExtent(copies.front()).x,(16+2*tier)*256.0,1e-8);
        int blocked=0,open=0;
        for(int y=1;y<map->width()-1;++y)for(int x=1;x<map->width()-1;++x){
            const Vec2 probe{(x+.5)*256,(y+.5)*256};
            if(terrain.blocked(probe,copies.front())){
                ++blocked;CHECK(distance(terrain.resolveCircle(probe,25,copies.front()),probe)>1);
            }else ++open;
        }
        CHECK(blocked>0);CHECK(open>0);
        const auto at=map->defaultSpawn(rng,terrain);CHECK(!terrain.blocked(at,copies.front()));CHECK_NEAR(distance(terrain.resolveCircle(at,25,copies.front()),at),0,1e-6);
    }
}
TEST(feedback_pharaoh_has_no_low_rarity_ambient_variants) {
    CHECK(feedbackContent());const auto id=content().mobIndex("pharaoh_beetle");CHECK(id!=kInvalidIndex);
    for(int tier=0;tier<rarityIndex(Rarity::Super);++tier)CHECK(!content().mobStats(id,static_cast<Rarity>(tier)).ambient);
    CHECK(content().mobStats(id,Rarity::Super).ambient);
}
TEST(feedback_ant_hell_supers_cover_black_fire_and_termite_habitats) {
    CHECK(feedbackContent());WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(testsupport::dataDir(),&terrain,error));bool found=false;
    const auto realm=maps.realmOfId("ant_hell",found);CHECK(found);const auto* map=maps.forRealm(realm);CHECK(map);if(!map)return;
    std::vector<RealmPoint> viewers;Rng rng{129};
    for(const auto& e:map->elements())if(e.isSpawnBand()&&!e.singular&&familyOf(e)>=0){
        for(int n=0;n<1000;++n){Vec2 at{rng.range(e.bounds.x,e.bounds.right()),rng.range(e.bounds.y,e.bounds.bottom())};if(e.contains(at)&&!terrain.blocked(at,realm)){viewers.push_back({at,realm});break;}}
    }
    CHECK(!viewers.empty());World w;CommandBuffer commands{w};SpawnSystem spawns;spawns.worldMaps=&maps;
    for(int tick=0;tick<100;++tick){spawns.run(w,terrain,content(),viewers,rng,tick*net::kTickMillis,net::kTickSeconds,commands);commands.flush();}
    int supers=0,habitats[3]={};Query<MobTag,MobType,Transform> mobs{w};
    mobs.each([&](Entity,MobTag&,MobType& type,Transform& at){if(at.realm!=realm||type.rarity!=Rarity::Super)return;++supers;
        for(const auto& e:map->elements())if(e.isSpawnBand()&&e.contains(at.position)){const int f=familyOf(e);if(f>=0){++habitats[f];break;}}
    });
    std::printf("Ant Hell Supers %d; black %d fire %d termite %d\n",supers,habitats[0],habitats[1],habitats[2]);
    CHECK_EQ(supers,5);for(const auto n:habitats)CHECK(n>0);
}

TEST(feedback_mound_entry_selects_tier_map_and_exposes_exterior_marker) {
    CHECK(feedbackContent());WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    bool found=false;const auto home=maps.realmOfId("jungle",found);CHECK(found);if(!found)return;Rng rng{88};
    for(const auto tier:{Rarity::Uncommon,Rarity::Super}){
        World w;CommandBuffer commands{w};SpawnSystem spawn;CombatSystem combat;DungeonSystem dungeon;dungeon.worldMaps=&maps;
        const auto at=maps.forRealm(home)->defaultSpawn(rng,terrain);
        const auto nest=spawn.spawnMob(w,terrain,content(),content().mobIndex("termite_mound"),tier,at,home,0,rng);CHECK(nest!=NULL_ENTITY);if(nest==NULL_ENTITY)continue;
        CHECK(!w.has<Spawner>(nest));CHECK(!w.has<NestWaves>(nest));CHECK(!w.has<AmbushNest>(nest));
        const auto entrance=w.get<Transform>(nest).position;const auto player=w.create();
        w.add<PlayerTag>(player);w.add<Transform>(player,Transform{entrance,0,home});w.add<Body>(player,Body{25,1});w.add<Health>(player,Health{1000,1000});
        dungeon.moveToRealm=[&](Entity e,Realm realm,Vec2 p){auto& t=w.get<Transform>(e);t.realm=realm;t.position=p;};
        dungeon.run(w,terrain,content(),spawn,combat,rng,900,commands);commands.flush();
        CHECK(w.get<Transform>(player).realm==home);
        dungeon.requestEntry(player,1000);
        dungeon.run(w,terrain,content(),spawn,combat,rng,1000,commands);commands.flush();
        dungeon.run(w,terrain,content(),spawn,combat,rng,1700,commands);commands.flush();
        const auto inside=w.get<Transform>(player).realm;CHECK(inside!=home);CHECK(dungeon.isInstance(inside));
        int brood=0;Query<DungeonDweller,Transform> insideMobs{w};
        insideMobs.each([&](Entity,DungeonDweller& dweller,Transform& at){if(dweller.entrance==nest){++brood;CHECK(at.realm==inside);}});
        CHECK(brood>0);
        CHECK_EQ(maps.forRealm(inside)->width(),16+2*rarityIndex(tier));Realm exterior;Vec2 projected;
        CHECK(dungeon.exteriorForRealm(inside,exterior,projected));CHECK(exterior==home);CHECK_NEAR(distance(projected,entrance),0,1e-6);
        NetClient::OnlineRosterEntry row;row.placed=true;row.realm=static_cast<std::uint8_t>(inside);row.x=100;row.y=200;
        row.insideDungeon=true;row.exteriorRealm=static_cast<std::uint8_t>(home);row.exteriorX=projected.x;row.exteriorY=projected.y;
        Vec2 marker;bool outside=false;CHECK(row.mapPosition(static_cast<std::uint8_t>(home),marker,outside));CHECK(outside);CHECK_NEAR(distance(marker,entrance),0,.01);
        CHECK(row.mapPosition(static_cast<std::uint8_t>(inside),marker,outside));CHECK(!outside);CHECK_NEAR(marker.x,100,1e-6);
    }
}

TEST(feedback_talent_effects_reach_live_petals_and_locked_slots_stay_owned) {
    CHECK(feedbackContent());World w;CommandBuffer commands{w};PetalSystem petals;const auto player=w.create();
    w.add<PlayerTag>(player);w.add<Transform>(player,Transform{{10000,10000},0});w.add<Motion>(player);w.add<Body>(player,Body{25,1});
    w.add<Health>(player,Health{1000,1000});w.add<Faction>(player,Faction{Team::Players,false});w.add<PlayerProgress>(player);
    w.add<PlayerInput>(player);w.add<PlayerModifiers>(player);w.add<Loadout>(player);w.add<PetalRing>(player);w.add<Afflictions>(player);w.add<HitCooldowns>(player);w.add<PlayerSkillTree>(player);
    const auto basic=content().petalIndex("basic");w.get<Loadout>(player).slots[0].configIndex=basic;w.get<Loadout>(player).slots[5].configIndex=basic;
    double now=1000;auto tick=[&](int count){for(int i=0;i<count;++i){now+=net::kTickMillis;petals.run(w,content(),now,net::kTickSeconds,commands);commands.flush();}};
    auto countSlot=[&](int slot){int n=0;Query<PetalInstance> q{w};q.each([&](Entity,PetalInstance& p){if(p.owner==player&&p.slot==slot)++n;});return n;};
    tick(100);CHECK(countSlot(0)>0);CHECK_EQ(countSlot(5),0);CHECK_EQ(w.get<Loadout>(player).slots[5].configIndex,basic);
    w.get<PlayerSkillTree>(player).skills.set(SkillId::Loadout,0);tick(100);CHECK(countSlot(5)>0);
    auto& skills=w.get<PlayerSkillTree>(player).skills;skills.set(SkillId::Loadout,4);skills.set(SkillId::Magnetism,0);skills.set(SkillId::Rotation,0);skills.set(SkillId::Reach,2);skills.set(SkillId::Luck,8);skills.set(SkillId::SharpEdges,0);
    const auto spin=w.get<PetalRing>(player).spin;tick(1);
    const auto mods=w.get<PlayerModifiers>(player);CHECK_NEAR(mods.damageScale,1.38,1e-8);CHECK_NEAR(mods.luck,3.7,1e-8);CHECK_NEAR(mods.attackRange,150,1e-8);
    CHECK_NEAR(mods.magnetism-kBaseMagnetism,1000,1e-8);CHECK_NEAR(wrapAngle(w.get<PetalRing>(player).spin-spin),3.1*net::kTickSeconds,1e-8);
    w.get<PlayerSkillTree>(player).skills.set(SkillId::PetalHealth,8);const auto stinger=content().petalIndex("stinger");w.get<Loadout>(player).slots[0].configIndex=stinger;tick(400);
    int checked=0;Query<PetalInstance,Health> q{w};q.each([&](Entity,PetalInstance& p,Health& health){if(p.owner==player&&p.configIndex==stinger){++checked;CHECK_NEAR(health.max,1,1e-8);}});CHECK(checked>0);
}

TEST(feedback_flowers_can_stand_inside_moon_without_shoving_it) {
    World w;Terrain terrain;MovementSystem movement;const auto player=w.create(),moon=w.create();
    const Vec2 start{5000,5000},moonAt{5010,5000};
    w.add<PlayerTag>(player);w.add<Transform>(player,Transform{start,0});w.add<Motion>(player);
    w.add<Body>(player,Body{25,1});w.add<PlayerInput>(player);w.add<PlayerModifiers>(player);w.add<Health>(player,Health{100,100});
    w.add<LoosePetal>(moon);w.add<RingAnchor>(moon);w.add<Transform>(moon,Transform{moonAt,0});w.add<Body>(moon,Body{100,10});
    for(int i=0;i<120;++i)movement.run(w,terrain,i*net::kTickMillis,net::kTickSeconds);
    CHECK_NEAR(distance(w.get<Transform>(player).position,start),0,1e-6);
    CHECK_NEAR(distance(w.get<Transform>(moon).position,moonAt),0,1e-6);
}
TEST(feedback_bubble_momentum_and_changed_equipment_hull_do_not_trap_a_player_in_wall) {
    World w;Terrain terrain;MovementSystem movement;
    const double wall=10*kTileSize;
    for(int y=0;y<terrain.tileRows();++y)terrain.setTile(10,y,Tile::Solid);
    const auto p=w.create();
    w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{wall-90,5000},0});
    w.add<Motion>(p,Motion{{20000,0}});w.add<Body>(p,Body{10,1});
    w.add<PlayerInput>(p);w.add<PlayerModifiers>(p);w.add<Health>(p,Health{100,100});
    movement.run(w,terrain,0,net::kTickSeconds);
    // A different loadout grows the hull while the burst still has momentum.
    w.get<Body>(p).radius=45;
    for(int n=1;n<=30;++n){
        movement.run(w,terrain,n*net::kTickMillis,net::kTickSeconds);
        const auto at=w.get<Transform>(p).position;
        CHECK(at.x<=wall-45+1e-6);CHECK(!terrain.resolveWall(at,45,Realm::Overworld).unresolved);
    }
    w.get<PlayerInput>(p).current.moveAngle=kPi;
    w.get<PlayerInput>(p).current.moveStrength=1;
    const auto before=w.get<Transform>(p).position;
    for(int n=31;n<=90;++n)movement.run(w,terrain,n*net::kTickMillis,net::kTickSeconds);
    CHECK(w.get<Transform>(p).position.x<before.x-100);
}

TEST(feedback_chip_has_independent_ninety_percent_self_evasion_but_not_lightning){
    CHECK(feedbackContent());const auto chipIndex=content().petalIndex("poker_chip");
    for(int tier=0;tier<kLadderRarityCount;++tier){const auto stats=content().petalStats(chipIndex,static_cast<Rarity>(tier));CHECK_NEAR(stats.petalEvasion,.9,1e-12);CHECK(stats.petalPoisonImmune);}
    World w;CommandBuffer commands{w};PetalSystem petals;CombatSystem combat;const auto player=w.create();
    w.add<PlayerTag>(player);w.add<Transform>(player,Transform{{10000,10000},0});w.add<Motion>(player);w.add<Body>(player,Body{25,1});
    w.add<Health>(player,Health{1000,1000});w.add<Faction>(player,Faction{Team::Players,false});w.add<PlayerProgress>(player);
    w.add<PlayerInput>(player);w.add<PlayerModifiers>(player);w.add<Loadout>(player);w.add<PetalRing>(player);w.add<Afflictions>(player);w.add<HitCooldowns>(player);w.add<PlayerSkillTree>(player);
    w.get<Loadout>(player).slots[0].configIndex=chipIndex;double now=1000;
    for(int n=0;n<100;++n){now+=net::kTickMillis;petals.run(w,content(),now,net::kTickSeconds,commands);commands.flush();}
    Entity chip=NULL_ENTITY;Query<PetalInstance,Health> q{w};q.each([&](Entity e,PetalInstance& p,Health&){if(p.owner==player&&p.configIndex==chipIndex)chip=e;});
    CHECK(chip!=NULL_ENTITY);if(chip==NULL_ENTITY)return;
    CHECK_NEAR(w.get<Evasion>(chip).chance,.9,1e-12);CHECK(w.has<PoisonImmune>(chip));CHECK_NEAR(w.get<PlayerModifiers>(player).evasion,0,1e-12);
    const auto enemy=w.create();w.add<MobTag>(enemy);w.add<Health>(enemy,Health{1e8,1e8});w.add<Faction>(enemy,Faction{Team::Hostiles,false});
    w.get<Health>(chip).current=1e8;w.get<Health>(chip).max=1e8;
    int dodges=0;for(int n=0;n<20000;++n){now+=1000;const auto hit=combat.applyDamage(w,chip,enemy,1,now,DamageKind::Recoil);CHECK(!hit.refused);if(hit.dodged)++dodges;}
    CHECK(dodges>17600&&dodges<18400);
    dodges=0;for(int n=0;n<2000;++n){now+=1000;const auto hit=combat.applyDamage(w,chip,enemy,1,now,DamageKind::Direct);if(hit.dodged)++dodges;}
    CHECK(dodges>1720&&dodges<1880);
    for(int n=0;n<100;++n){now+=1000;const auto hit=combat.applyDamage(w,chip,enemy,1,now,DamageKind::Lightning);CHECK(!hit.dodged);CHECK_NEAR(hit.applied,1,1e-12);}
    const auto poison=combat.applyDamage(w,chip,enemy,10,now+1000,DamageKind::Poison);CHECK(poison.refused);CHECK_NEAR(poison.applied,0,1e-12);
    combat.applyPoison(w,chip,enemy,10,1000,now+2000);const auto* status=w.tryGet<Afflictions>(chip);CHECK(!status||status->poisonPerSecond==0);
    const auto ownerHit=combat.applyDamage(w,player,enemy,1,now+3000,DamageKind::Direct);CHECK(!ownerHit.dodged);CHECK_NEAR(ownerHit.applied,1,1e-12);
}
