#include "test.h"
#include "test_data.h"
#include "server/systems/spawning.h"
#include "server/systems/loot.h"
using namespace flix;
namespace {
const ContentRegistry& data(){static ContentRegistry c;static bool ready=false;if(!ready){std::string e;CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),e));ready=true;}return c;}
int population(World&w,Realm realm,Rarity rarity){int n=0;Query<MobTag,MobType,Transform,AmbientMob> q{w};q.without<Dead>();q.each([&](Entity e,MobTag&,MobType& t,Transform& at,AmbientMob&){const auto* b=w.tryGet<BodySegment>(e);if(at.realm==realm&&t.rarity==rarity&&(!b||b->chainHead==e))++n;});return n;}
}
TEST(hyper_health_is_six_times_eternal_without_other_stat_inflation){
    for(std::uint16_t i=0;i<data().mobCount();++i){const auto e=data().mobStats(i,Rarity::Unique),h=data().mobStats(i,Rarity::Apex);
        CHECK_NEAR(h.health,e.health*6,1e-5);CHECK_NEAR(h.damage,e.damage*3,1e-5);
        CHECK_NEAR(h.xp,e.xp*3,1e-5);
        CHECK_NEAR(h.speed,e.speed,1e-9);CHECK_NEAR(h.chaseSpeed,e.chaseSpeed,1e-9);CHECK_NEAR(h.armor,e.armor,1e-9);}
}
TEST(hyper_one_per_regular_map_preserves_supers_eternals_and_waits_thirty_minutes){
    WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    World w;CommandBuffer commands{w};SpawnSystem spawn;spawn.worldMaps=&maps;Rng rng{5832};
    auto step=[&](double now){spawn.run(w,terrain,data(),std::vector<RealmPoint>{},rng,now,net::kTickSeconds,commands);commands.flush();};
    step(1000);for(auto& clock:spawn.bossClocks()){clock.uniqueReadyMillis=0;clock.apexReadyMillis=0;}
    for(int i=0;i<30;++i)step(2000+i*500);
    int checked=0;Entity boss=NULL_ENTITY;bool found=false;const auto garden=maps.realmOfId("garden",found);CHECK(found);
    for(const auto& map:maps.maps())if(!maps.isPrestige(map.realm())&&map.usesReferenceSpawnCurve()){
        CHECK_EQ(population(w,map.realm(),Rarity::Apex),1);CHECK_EQ(population(w,map.realm(),Rarity::Unique),3);CHECK_EQ(population(w,map.realm(),Rarity::Super),5);++checked;
    }
    CHECK_EQ(checked,9);Query<MobTag,MobType,Transform,AmbientMob> q{w};q.without<Dead>();
    q.each([&](Entity e,MobTag&,MobType&t,Transform&at,AmbientMob&){const auto* b=w.tryGet<BodySegment>(e);if(t.rarity==Rarity::Apex&&at.realm==garden&&(!b||b->chainHead==e))boss=e;});
    CHECK(boss!=NULL_ENTITY);if(boss==NULL_ENTITY)return;
    w.get<Health>(boss).current*=.5;step(7200000);CHECK(w.isAlive(boss));CHECK(!w.has<Dead>(boss));
    w.add<Dead>(boss);step(7201000);CHECK_NEAR(spawn.bossCooldownLeft(Rarity::Apex,garden,7201000),1800000,1e-6);
    w.destroy(boss);step(9000999);CHECK_EQ(population(w,garden,Rarity::Apex),0);
    for(int i=0;i<20;++i)step(9001000+i*500);CHECK_EQ(population(w,garden,Rarity::Apex),1);
}
TEST(hyper_actual_loot_is_thirty_trials_of_super_eternal_materials_never_hyper){
    World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;LootSystem loot;Rng rng{81927};
    const auto player=w.create();w.add<PlayerTag>(player);w.add<Transform>(player,Transform{{10000,10000},0});
    w.add<Body>(player,Body{25,1});w.add<Health>(player,Health{100,100});
    for(int n=0;n<100;++n){const auto corpse=w.create();w.add<MobTag>(corpse);
        w.add<MobType>(corpse,MobType{data().mobIndex("sandstorm"),Rarity::Apex,1});
        w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100});
        Bounty bounty;bounty.credit(player,100);w.add<Bounty>(corpse,bounty);w.add<Dead>(corpse,Dead{player});
        loot.run(w,grid,data(),rng,n,0,commands,events);commands.flush();w.destroy(corpse);}
    int total=0,eternal=0;Query<DropTag,DropItem> q{w};q.each([&](Entity,DropTag&,DropItem& item){
        CHECK(item.rarity==Rarity::Super||item.rarity==Rarity::Unique);total+=item.quantity;if(item.rarity==Rarity::Unique)eternal+=item.quantity;});
    CHECK_EQ(total,100*30*3);CHECK(eternal>0);CHECK(eternal<total);
}
