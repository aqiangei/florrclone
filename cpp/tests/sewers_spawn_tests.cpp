#include "test.h"
#include "test_data.h"
#include "server/systems/spawning.h"
using namespace flix;
TEST(sewers_reference_curve_has_ultras_in_real_high_threat_habitats) {
    ContentRegistry content;WorldMaps maps;Terrain terrain;std::string error;
    CHECK(content.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),error));
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));bool found=false;
    const auto realm=maps.realmOfId("sewers",found);CHECK(found);const auto* map=maps.forRealm(realm);
    CHECK(map&&map->usesReferenceSpawnCurve());if(!map)return;
    std::vector<RealmPoint> viewers;Rng points{718};
    for(const auto& e:map->elements())if(e.isSpawnBand()&&e.difficulty>=55&&!e.singular){
        for(int n=0;n<3000;++n){Vec2 at{points.range(e.bounds.x,e.bounds.right()),points.range(e.bounds.y,e.bounds.bottom())};
            if(e.contains(at)&&!terrain.blocked(at,realm)){viewers.push_back({at,realm});break;}}
    }
    CHECK(!viewers.empty());World world;CommandBuffer commands{world};SpawnSystem spawns;spawns.worldMaps=&maps;Rng rng{38};
    for(int i=0;i<100;++i){spawns.run(world,terrain,content,viewers,rng,i*net::kTickMillis,net::kTickSeconds,commands);commands.flush();}
    int ultras=0,total=0;Query<MobTag,MobType,Transform> mobs{world};
    mobs.each([&](Entity,MobTag&,MobType& type,Transform& at){if(at.realm==realm){++total;if(type.rarity==Rarity::Ultra)++ultras;}});
    std::printf("  Sewers live population: %d, Ultras: %d\n",total,ultras);CHECK(ultras>0);
}
TEST(sewers_reference_curve_matches_engine_tiers_and_tail_probability) {
    CHECK_EQ(referenceSpawnTier(15),2);CHECK_EQ(referenceSpawnTier(45),5);CHECK_EQ(referenceSpawnTier(65),6);
    Rng rng{982};int ultras=0;for(int i=0;i<100000;++i)if(rollReferenceSpawnRarity(55,1,rng)==Rarity::Ultra)++ultras;
    CHECK(ultras>12000&&ultras<15500);
}
