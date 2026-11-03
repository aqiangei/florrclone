#include "test.h"
#include "test_data.h"
#include "client/ui/menus.h"
#include "server/systems/combat.h"
#include "server/systems/movement.h"
#include "server/systems/spawning.h"
#include "shared/game/config.h"
using namespace flix;
namespace {
bool feedbackContent(){std::string error;return loadContent(testsupport::dataDir(),error);}
int antFamily(const std::string& id){return id.find("fire_ant")!=std::string::npos?1:id.find("termite")!=std::string::npos?2:id.find("ant")!=std::string::npos?0:-1;}
int familyOf(const MapElement& e){double weights[3]={};for(const auto& row:e.mobDistribution){const int f=antFamily(row.name);if(f>=0)weights[f]+=row.weight;}int best=-1;for(int n=0;n<3;++n)if(weights[n]>0&&(best<0||weights[n]>weights[best]))best=n;return best;}
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
        const auto* map=maps.forRealm(copies.front());CHECK(map);if(!map)continue;CHECK_EQ(map->width(),16+4*tier);
        CHECK_NEAR(terrain.realmExtent(copies.front()).x,(16+4*tier)*256.0,1e-8);
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
