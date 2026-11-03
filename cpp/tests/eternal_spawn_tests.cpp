#include "test.h"
#include "test_data.h"
#include "server/systems/spawning.h"
using namespace flix;
TEST(eternal_stats_are_three_times_super_without_speed_or_armor_inflation) {
    ContentRegistry c;std::string error;
    CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),error));
    for(std::uint16_t n=0;n<c.mobCount();++n){
        const auto s=c.mobStats(n,Rarity::Super),e=c.mobStats(n,Rarity::Unique);
        CHECK_NEAR(e.health,s.health*3,1e-5);CHECK_NEAR(e.damage,s.damage*3,1e-5);
        CHECK_NEAR(e.speed,s.speed,1e-9);CHECK_NEAR(e.chaseSpeed,s.chaseSpeed,1e-9);
        CHECK_NEAR(e.armor,s.armor,1e-9);
    }
}
TEST(eternal_one_per_regular_map_keeps_five_supers_and_waits_after_death) {
    ContentRegistry c;WorldMaps maps;Terrain terrain;std::string error;
    CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),error));
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    World w;CommandBuffer commands{w};SpawnSystem spawn;spawn.worldMaps=&maps;Rng rng{5832};
    auto step=[&](double now){spawn.run(w,terrain,c,std::vector<RealmPoint>{},rng,now,net::kTickSeconds,commands);commands.flush();};
    step(1000);for(auto& clock:spawn.bossClocks()){clock.uniqueReadyMillis=0;clock.apexReadyMillis=1e15;}
    for(int n=0;n<20;++n)step(2000+n*500);
    bool found=false;const auto garden=maps.realmOfId("garden",found);CHECK(found);
    Entity boss=NULL_ENTITY;int mapsChecked=0;
    for(const auto& map:maps.maps()) {
        if(maps.isPrestige(map.realm())||!map.usesReferenceSpawnCurve())continue;
        int eternal=0,supers=0;
        Query<MobTag,MobType,Transform,AmbientMob> q{w};q.without<Dead>();
        q.each([&](Entity e,MobTag&,MobType& type,Transform& at,AmbientMob&){
            const auto* segment=w.tryGet<BodySegment>(e);if(segment&&segment->chainHead!=e)return;
            if(at.realm!=map.realm())return;
            if(type.rarity==Rarity::Unique){++eternal;if(at.realm==garden)boss=e;}
            if(type.rarity==Rarity::Super)++supers;
        });
        std::printf("  %s: E=%d S=%d\n",map.id().c_str(),eternal,supers);
        CHECK_EQ(eternal,1);CHECK_EQ(supers,5);++mapsChecked;
    }
    CHECK_EQ(mapsChecked,9);CHECK(boss!=NULL_ENTITY);if(boss==NULL_ENTITY)return;
    w.get<Health>(boss).current*=0.5;
    step(7200000);CHECK(w.isAlive(boss));CHECK(!w.has<Dead>(boss));
    w.add<Dead>(boss);step(7201000);
    CHECK_NEAR(spawn.bossCooldownLeft(Rarity::Unique,garden,7201000),1200000,1e-6);
    w.destroy(boss);step(8400500);
    int count=0;auto countBosses=[&](){count=0;Query<MobTag,MobType,Transform,AmbientMob> q{w};q.without<Dead>();
        q.each([&](Entity e,MobTag&,MobType& type,Transform& at,AmbientMob&){const auto* b=w.tryGet<BodySegment>(e);if(at.realm==garden&&type.rarity==Rarity::Unique&&(!b||b->chainHead==e))++count;});};
    countBosses();CHECK_EQ(count,0);
    for(int n=0;n<10;++n)step(8401000+n*500);
    countBosses();CHECK_EQ(count,1);
}
TEST(eternal_restored_cooldown_survives_a_fresh_spawner) {
    ContentRegistry c;WorldMaps maps;Terrain terrain;std::string error;
    CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),error));
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    World world;CommandBuffer commands{world};SpawnSystem spawn;spawn.worldMaps=&maps;Rng rng{7823};
    // This is the persisted-deadline path the server uses on restart.
    spawn.restoreBossClock("garden",1200000,1e15);
    auto step=[&](double now){spawn.run(world,terrain,c,std::vector<RealmPoint>{},rng,now,net::kTickSeconds,commands);commands.flush();};
    bool found=false;const auto garden=maps.realmOfId("garden",found);CHECK(found);
    auto count=[&](){int n=0;Query<MobTag,MobType,Transform,AmbientMob> q{world};q.without<Dead>();
        q.each([&](Entity e,MobTag&,MobType& t,Transform& at,AmbientMob&){const auto* b=world.tryGet<BodySegment>(e);if(at.realm==garden&&t.rarity==Rarity::Unique&&(!b||b->chainHead==e))++n;});return n;};
    step(1000);step(1199999);CHECK_EQ(count(),0);
    for(int n=0;n<10;++n)step(1201000+n*500);
    CHECK_EQ(count(),1);
}
