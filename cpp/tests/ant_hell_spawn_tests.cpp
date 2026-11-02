#include "test.h"
#include "test_data.h"
#include "server/systems/spawning.h"
#include <map>

using namespace flix;

TEST(ant_hell_fire_ant_and_termite_habitats_wake_real_mobs_when_players_arrive){
    ContentRegistry content;WorldMaps maps;Terrain terrain;std::string error;
    CHECK(content.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),error));
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    bool found=false;const auto realm=maps.realmOfId("ant_hell",found);CHECK(found);
    const auto* map=maps.forRealm(realm);if(!map)return;
    bool pyramidFound=false;const auto* pyramid=maps.forRealm(maps.realmOfId("pyramid",pyramidFound));
    CHECK(pyramidFound);int pyramidBands=0;
    if(pyramid)for(const auto& e:pyramid->elements())if(e.isSpawnBand()){
        ++pyramidBands;CHECK_EQ(e.mobDistribution.size(),std::size_t(4));
        for(const auto& row:e.mobDistribution)CHECK(row.name=="mummy_beetle"||row.name=="nazar_beetle"||row.name=="pharaoh_beetle"||row.name=="tomb");
    }
    CHECK_EQ(pyramidBands,1);
    std::vector<RealmPoint> viewers;Rng points{127};
    for(const std::string family:{"fire_ant","termite"}){
        const MapElement* largest=nullptr;int regions=0;
        for(const auto& e:map->elements()){
            if(!e.isSpawnBand()||e.singular)continue;
            bool matches=false;for(const auto& row:e.mobDistribution)if(row.name.find(family)!=std::string::npos)matches=true;
            if(!matches)continue;++regions;
            if(!largest||e.area()>largest->area())largest=&e;
        }
        CHECK(regions>20);CHECK(largest!=nullptr);if(!largest)continue;
        int added=0;
        for(int n=0;n<10000&&added<3;++n){const Vec2 at{points.range(largest->bounds.x,largest->bounds.right()),points.range(largest->bounds.y,largest->bounds.bottom())};
            if(!largest->contains(at)||terrain.blocked(at,realm))continue;
            viewers.push_back({at,realm});++added;
        }
        CHECK_EQ(added,3);
    }
    World world;CommandBuffer commands{world};SpawnSystem spawn;spawn.worldMaps=&maps;Rng rng{472};
    // Start empty, then walk into both habitats: exercise latent stocking and
    // awakening, rather than merely counting authored rectangles.
    for(int i=0;i<20;++i){spawn.run(world,terrain,content,{},rng,i*net::kTickMillis,net::kTickSeconds,commands);commands.flush();}
    for(int i=20;i<80;++i){spawn.run(world,terrain,content,viewers,rng,i*net::kTickMillis,net::kTickSeconds,commands);commands.flush();}
    std::map<std::string,int> types;Query<MobTag,MobType,Transform> mobs{world};
    mobs.each([&](Entity,MobTag&,MobType& type,Transform& at){if(at.realm==realm)++types[content.mob(type.configIndex).id];});
    int fire=0,termites=0;
    for(const auto&[id,n]:types){if(id.find("fire_ant")!=std::string::npos)fire+=n;if(id.find("termite")!=std::string::npos)termites+=n;std::printf("  ant_hell %s: %d\n",id.c_str(),n);}
    CHECK(fire>5);CHECK(termites>5);
}
