#include "test.h"
#include "test_data.h"
#include "server_harness.h"
#include "server/loot_eligibility.h"
#include "server/systems/combat.h"
#include "server/systems/loot.h"
#include "server/systems/petals.h"
#include "server/systems/movement.h"
#include "shared/game/config.h"
#include "shared/game/computer_set.h"
#include "server/systems/spawning.h"
#include <chrono>
using namespace flix;

TEST(mechanics1105_loot_stress_preserves_stacks_and_entitlements) {
    const auto& c=testsupport::shippedContent();World w;CommandBuffer commands{w};EventQueue events;
    SpatialGrid grid;LootSystem loot;Rng rng{81927};std::vector<Entity> players;
    for(int i=0;i<2;++i){const auto p=w.create();players.push_back(p);w.add<PlayerTag>(p);
        w.add<Transform>(p,Transform{{100000.0+i*100,100000},0});w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100});}
    const auto basic=c.petalIndex("basic");
    for(int i=0;i<12000;++i){auto e=loot.spawnDrop(w,basic,Rarity::Common,
        {300.0*(i%100)-15000,300.0*(i/100)-15000},i%2?Realm::Arena:Realm::Overworld,
        i%3?players:std::vector<Entity>{players[0]});CHECK(e!=NULL_ENTITY);
        if(i%7==0)w.get<DropItem>(e).pickedUpBy.push_back(LootClaim{players[0],0});}
    for(int i=0;i<100;++i){const auto e=w.create();w.add<MobTag>(e);
        w.add<MobType>(e,MobType{c.mobIndex("sandstorm"),Rarity::Apex,1});
        w.add<Transform>(e,Transform{{(i%4)*170.0-250,(i/4)*25.0-100},0});w.add<Health>(e,Health{0,100});
        Bounty bounty;bounty.credit(players[0],100);if(i%2)bounty.credit(players[1],100);
        w.add<Bounty>(e,bounty);w.add<Dead>(e,Dead{players[0]});}
    auto start=std::chrono::steady_clock::now();loot.run(w,grid,c,rng,1000,0,commands,events);commands.flush();
    const double millis=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::uint64_t hash=1469598103934665603ull;std::vector<std::string> rows;std::uint64_t copies=0;
    Query<DropTag,DropItem,Transform> q{w};q.each([&](Entity,DropTag&,DropItem& item,Transform& at){
        copies+=item.quantity;std::string row=std::to_string(item.configIndex)+":"+std::to_string(rarityIndex(item.rarity))+":"+
        std::to_string(static_cast<unsigned>(at.realm))+":"+std::to_string(at.position.x)+":"+std::to_string(at.position.y)+":"+std::to_string(item.quantity);
        for(const auto& claim:item.eligible)row+="/e"+std::to_string(claim.body)+":"+std::to_string(claim.owner);
        for(const auto& claim:item.pickedUpBy)row+="/p"+std::to_string(claim.body)+":"+std::to_string(claim.owner);
        rows.push_back(std::move(row));});
    std::sort(rows.begin(),rows.end());for(const auto& row:rows)for(unsigned char ch:row){hash^=ch;hash*=1099511628211ull;}
    CHECK(copies>=21000);CHECK(rows.size()>=12000);
    std::printf("loot_stress: %.3f ms, %zu entities, %llu copies, checksum %llu\n",millis,rows.size(),
        static_cast<unsigned long long>(copies),static_cast<unsigned long long>(hash));
    // Already-awarded corpses and a no-death frame cannot pay out again.
    loot.run(w,grid,c,rng,1050,0,commands,events);commands.flush();std::uint64_t after=0;
    q.each([&](Entity,DropTag&,DropItem& item,Transform&){after+=item.quantity;});CHECK_EQ(after,copies);
}

TEST(mechanics1105_rotation_wire_bounds_and_actual_ring_speed) {
    const auto& c=testsupport::shippedContent();
    for(const int step:{0,1,2,3,4,255}) {
        net::InputFrame sent;sent.rotationStep=step;sent.sequence=123;sent.aimDistance=456;
        ByteWriter bytes;sent.write(bytes);ByteReader reader(bytes.data(),bytes.size());
        auto got=net::InputFrame::read(reader);CHECK(reader.ok());CHECK_EQ(got.rotationStep,step);
        CHECK_EQ(got.sequence,123u);CHECK_EQ(got.aimDistance,456);
        World w;PetalSystem petals;CommandBuffer commands{w};const auto p=w.create();
        w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{5000,5000},0});w.add<Body>(p,Body{25,1});
        w.add<Health>(p,Health{100,100});w.add<PlayerModifiers>(p);w.add<Loadout>(p);w.add<PetalRing>(p);
        w.add<PlayerInput>(p).current=got;
        petals.run(w,c,1000,.05,commands);
        CHECK_NEAR(w.get<PetalRing>(p).spin,kPetalSpinRate*.05*std::clamp(step,1,4)*.25,1e-8);
    }
}

TEST(mechanics1105_computer_set_requires_distinct_equipped_parts_at_matching_tier) {
    const auto& c=testsupport::shippedContent();World w;PetalSystem petals;CommandBuffer commands{w};
    const auto p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{5000,5000},0});
    w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100});w.add<PlayerModifiers>(p);
    w.add<PetalRing>(p);w.add<Loadout>(p);
    const auto equip=[&](int slot,const char* id,Rarity rarity){auto& s=w.get<Loadout>(p).slots[slot];s.configIndex=c.petalIndex(id);s.rarity=rarity;};
    equip(0,"cube",Rarity::Apex);equip(1,"ram",Rarity::Apex);equip(2,"computer_chip",Rarity::Common);
    equip(3,"mimic",Rarity::Apex);equip(4,"ram",Rarity::Apex);equip(10,"capacitor",Rarity::Apex);
    petals.foldModifiers(w,c);
    CHECK_EQ(computerPartCount(w.get<PlayerModifiers>(p).computerParts[rarityIndex(Rarity::Apex)]),2);
    CHECK_EQ(computerPartCount(w.get<PlayerModifiers>(p).computerParts[rarityIndex(Rarity::Common)]),3);
    equip(2,"computer_chip",Rarity::Apex);equip(3,"capacitor",Rarity::Apex);equip(4,"cursor",Rarity::Apex);
    petals.foldModifiers(w,c);
    petals.run(w,c,1000,.05,commands);
    petals.run(w,c,10000,.05,commands); // serve the real equip cooldown before damaging the ring
    const auto base=c.petalStats(c.petalIndex("cube"),Rarity::Apex);
    const auto boosted=computerSetStats(c,w,p,c.petalIndex("cube"),Rarity::Apex,base);
    CHECK_NEAR(boosted.damage,base.damage*1.12,1e-6);CHECK_NEAR(boosted.health,base.health*1.15,1e-6);
    CHECK_NEAR(boosted.reloadMillis,base.reloadMillis*.9,1e-6);
    const auto originalRose=c.petalStats(c.petalIndex("rose"),Rarity::Apex);
    CHECK_NEAR(computerSetStats(c,w,p,c.petalIndex("rose"),Rarity::Apex,originalRose).heal,originalRose.heal,1e-6);
    auto negative=c.petalStats(c.petalIndex("glitch"),Rarity::Apex);const auto oldDamage=negative.damage;
    applyComputerSet(c.petal(c.petalIndex("glitch")),63,negative);CHECK_NEAR(negative.damage,oldDamage,1e-6);
    // Changing the set must preserve damage already taken, not heal the petal.
    auto& state=w.get<PetalSlotState>(p).slots[0];CHECK(state.poolMax>0);
    int damaged=0;
    for(const auto e:w.get<Loadout>(p).spawned)if(w.get<PetalInstance>(e).slot==0){w.get<Health>(e).current*=.5;++damaged;}
    CHECK(damaged>0);
    w.get<Loadout>(p).slots[3]=LoadoutSlot{};w.get<Loadout>(p).slots[4]=LoadoutSlot{};
    petals.foldModifiers(w,c);petals.run(w,c,10050,.05,commands);
    CHECK_NEAR(w.get<PetalSlotState>(p).slots[0].poolMax,base.health,1e-6);
    CHECK_NEAR(w.get<PetalSlotState>(p).slots[0].poolHealth,base.health*.5,1e-6);
}

TEST(mechanics1105_hyper_mob_contact_and_egg_guard_do_not_protect_against_spells_or_higher_tiers) {
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));const auto& c=content();
    World w;Terrain terrain;SpawnSystem spawns;Rng rng{72};
    const auto mobIndex=c.mobIndex("ladybug");
    for(const auto rarity:{Rarity::Super,Rarity::Unique,Rarity::Apex}) {
        const auto e=spawns.spawnMob(w,terrain,c,mobIndex,rarity,{5000,5000},Realm::Overworld,1000,rng);
        CHECK(e!=NULL_ENTITY);if(e==NULL_ENTITY)continue;
        CHECK_NEAR(w.get<ContactDamage>(e).amount,c.mobStats(mobIndex,rarity).damage*(rarity==Rarity::Apex?.5:1),1e-5);
    }
    const auto owner=w.create();w.add<PlayerTag>(owner);w.add<Loadout>(owner);
    w.get<Loadout>(owner).slots[0].configIndex=c.petalIndex("egg");w.get<Loadout>(owner).slots[0].rarity=Rarity::Apex;
    const auto source=w.create();w.add<MobTag>(source);w.add<MobType>(source,MobType{mobIndex,Rarity::Apex,1});
    w.add<Faction>(source,Faction{Team::Hostiles});w.add<Transform>(source,Transform{{5000,5000},0});
    CombatSystem combat;
    const auto victim=[&](){const auto e=w.create();w.add<MobTag>(e);w.add<Pet>(e,Pet{owner,0});
        w.add<Faction>(e,Faction{Team::Players});w.add<Health>(e,Health{100,100});
        w.add<Transform>(e,Transform{{5010,5000},0});return e;};
    auto pet=victim();combat.applyDamage(w,pet,source,1000,1000);CHECK_NEAR(w.get<Health>(pet).current,65,1e-8);
    combat.applyDamage(w,pet,source,1000,1100);CHECK_NEAR(w.get<Health>(pet).current,30,1e-8);
    combat.applyDamage(w,pet,source,1000,1200);CHECK(w.has<Dead>(pet));
    pet=victim();combat.applyDamage(w,pet,source,1000,1000,DamageKind::Lightning);CHECK(w.has<Dead>(pet));
    w.get<MobType>(source).rarity=static_cast<Rarity>(rarityIndex(Rarity::Apex)+1);
    pet=victim();combat.applyDamage(w,pet,source,1000,1000);CHECK(w.has<Dead>(pet));
    w.get<MobType>(source).rarity=Rarity::Apex;w.get<Loadout>(owner).slots[0].rarity=Rarity::Unique;
    pet=victim();combat.applyDamage(w,pet,source,1000,1000);CHECK(w.has<Dead>(pet));
}
TEST(hyper_corruption_aura_respects_radius_realm_level_and_never_chains) {
    const auto& c=testsupport::shippedContent();World w;PetalSystem petals;CommandBuffer commands{w};
    const auto flower=[&](double x,int level=20,Realm realm=Realm::Overworld){
        const auto p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{x,1000},0,realm});
        w.add<Motion>(p);w.add<Body>(p,Body{kPlayerBaseRadius,1});w.add<Health>(p,Health{100,100});
        w.add<PlayerModifiers>(p);w.add<PetalRing>(p);w.add<Loadout>(p);w.add<PlayerInput>(p);
        w.add<PlayerVisuals>(p);w.add<PlayerProgress>(p).level=level;return p;
    };
    const auto source=flower(1000),edge=flower(1600),outside=flower(1601),chain=flower(2100),low=flower(1200,19),other=flower(1000,20,Realm::Arena);
    auto& slot=w.get<Loadout>(source).slots[0];slot.configIndex=c.petalIndex("corruption");slot.rarity=Rarity::Apex;
    petals.run(w,c,1000,1.0/60,commands);
    CHECK(w.get<PlayerVisuals>(edge).nearbyCorruption);CHECK(!w.get<PlayerVisuals>(outside).nearbyCorruption);
    CHECK(!w.get<PlayerVisuals>(chain).nearbyCorruption);CHECK(!w.get<PlayerVisuals>(low).nearbyCorruption);CHECK(!w.get<PlayerVisuals>(other).nearbyCorruption);
    w.get<Transform>(edge).position.x=1601;petals.run(w,c,1017,1.0/60,commands);CHECK(!w.get<PlayerVisuals>(edge).nearbyCorruption);
    w.get<Transform>(edge).position.x=1600;w.get<Loadout>(source).slots[0].rarity=Rarity::Unique;
    petals.run(w,c,1034,1.0/60,commands);CHECK(!w.get<PlayerVisuals>(edge).nearbyCorruption);
    w.get<Loadout>(source).slots[0].rarity=Rarity::Apex;petals.run(w,c,1051,1.0/60,commands);CHECK(w.get<PlayerVisuals>(edge).nearbyCorruption);
    w.add<Dead>(source);petals.run(w,c,1068,1.0/60,commands);CHECK(!w.get<PlayerVisuals>(edge).nearbyCorruption);
}
TEST(feedback1105_corrupted_killer_preserves_mob_damage_and_respawn_loot) {
    const auto& c=testsupport::shippedContent();World w;CombatSystem combat;
    const auto victim=w.create();w.add<PlayerTag>(victim);w.add<Health>(victim,Health{100,100});
    w.add<Faction>(victim,Faction{Team::Players,false});w.add<PlayerAccount>(victim,PlayerAccount{"1","victim",42,false});
    w.add<Transform>(victim,Transform{{1000,1000},0});
    const auto killer=w.create();w.add<PlayerTag>(killer);w.add<Health>(killer,Health{100,100});
    w.add<Faction>(killer,Faction{Team::Players,false});w.add<PlayerVisuals>(killer).corrupted=true;
    const auto mob=w.create();w.add<MobTag>(mob);w.add<Bounty>(mob);w.add<MobType>(mob,MobType{c.mobIndex("sandstorm"),Rarity::Super,1});
    w.add<Transform>(mob,Transform{{1000,1000},0});w.add<Health>(mob,Health{100,100});w.add<Faction>(mob,Faction{Team::Hostiles,false});
    combat.applyDamage(w,mob,victim,20,1000);CHECK_EQ(w.get<Bounty>(mob).contributors.size(),std::size_t(1));
    combat.applyDamage(w,victim,killer,1000,1001);CHECK(w.has<Dead>(victim));
    CHECK_NEAR(w.get<Bounty>(mob).contributors.front().damage,20,1e-9);
    w.destroy(victim);const auto reborn=w.create();w.add<PlayerTag>(reborn);w.add<Health>(reborn,Health{100,100});
    w.add<Transform>(reborn,Transform{{10000,10000},0});w.add<PlayerAccount>(reborn,PlayerAccount{"1","victim",42,false});
    CHECK_EQ(refreshedShare(w,w.get<Bounty>(mob).contributors.front()).player,reborn);
    w.get<Health>(mob).current=0;w.add<Dead>(mob,Dead{killer});
    CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{82};LootSystem loot;
    loot.run(w,grid,c,rng,1002,0,commands,events);commands.flush();int count=0;
    Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& d){if(LootSystem::mayPickUp(d,reborn,42))count+=d.quantity;});CHECK(count>0);
}
TEST(feedback1105_crafting_and_ruby_limits) {
    CHECK(craftsOutOf(Rarity::Ultra));CHECK(!craftsOutOf(Rarity::Super));CHECK(!craftsOutOf(Rarity::Unique));
    CHECK_EQ(oracleCraftCost(Rarity::Super),0);
    const auto& c=testsupport::shippedContent();CHECK_NEAR(c.petalStats(c.petalIndex("ruby"),Rarity::Common).lightningRepeatChance,.4,1e-9);
    CHECK(c.petalStats(c.petalIndex("ruby"),Rarity::Apex).lightningRepeatChance<=.55);
    CHECK(c.petalIndex("cutter")<c.petalCount());CHECK(c.mobIndex("digger")<c.mobCount());
}

TEST(feedback1105_powder_keeps_acceleration_and_speed_at_20_30_and_60_tps) {
    const auto& c=testsupport::shippedContent();double speeds[3]={};int index=0;
    for(int hz:{20,30,60}) {
        World w;Terrain terrain;MovementSystem movement;PetalSystem petals;const auto p=w.create();
        w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{5000,5000},0});w.add<Motion>(p);
        w.add<Body>(p,Body{kPlayerBaseRadius,1});w.add<Health>(p,Health{100,100});w.add<PlayerModifiers>(p);
        w.add<PetalRing>(p);auto& load=w.add<Loadout>(p);load.slots[0].configIndex=c.petalIndex("mysterious_powder");load.slots[0].rarity=Rarity::Super;
        auto& input=w.add<PlayerInput>(p);input.current.moveStrength=1;input.current.moveAngle=0;
        petals.foldModifiers(w,c);const double scale=w.get<PlayerModifiers>(p).speedScale;CHECK(scale>1);
        for(int i=0;i<hz;++i)movement.run(w,terrain,i*1000.0/hz,1.0/hz);
        speeds[index++]=w.get<Motion>(p).velocity.x;
        CHECK_NEAR(speeds[index-1],kPlayerMaxSpeed*scale,1.0);
    }
    CHECK_NEAR(speeds[0],speeds[1],1e-8);
    CHECK_NEAR(speeds[0],speeds[2],1e-8);
}

TEST(feedback1105_server_rejects_super_and_eternal_bulk_without_consuming_inventory) {
    using namespace testsupport;
    Harness h("feedback1105-craft",[](const std::string& path){
        seedUser(path,"feedbacksmith","StrongTest42!");
        seedStack(path,"feedbacksmith","petal_rose",Rarity::Super,100);
        seedStack(path,"feedbacksmith","petal_rose",Rarity::Common,100);
    });CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(connectClient(h,client));client.requestLogin("feedbacksmith","StrongTest42!");
    CHECK(h.stepUntil({&client},[&]{return client.status()==NetClient::Status::LoggedIn;}));
    const auto rose=content().petalIndex("rose");
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.stackCount(rose,Rarity::Super)==100;}));
    client.requestCraft(rose,Rarity::Super,100);
    CHECK(h.stepUntil({&client},[&]{return client.craftOutcome().pending;}));
    CHECK(!client.craftOutcome().success);CHECK_EQ(client.profile().stackCount(rose,Rarity::Super),100u);
    client.requestBulkCraft(Rarity::Unique);h.step(120,{&client});
    CHECK_EQ(client.profile().stackCount(rose,Rarity::Common),100u);
    CHECK_EQ(client.profile().stackCount(rose,Rarity::Super),100u);
    CHECK_EQ(client.profile().stackCount(rose,Rarity::Unique),0u);
}
