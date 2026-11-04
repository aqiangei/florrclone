#include "test.h"
#include "test_data.h"
#include "server/systems/npcs.h"
using namespace flix;
TEST(assemblers_keep_only_eternal_at_small_collision_body){
    const auto& c=testsupport::shippedContent();WorldMaps maps;Terrain terrain;std::string error;
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));NpcSystem npcs;std::vector<std::string> warnings;
    npcs.loadSites(maps,c,warnings);World world;npcs.placeMissing(world,terrain,c,0);
    for(const auto* mapId:{"computer","cyberspace"}){
        bool found=false;const auto realm=maps.realmOfId(mapId,found);CHECK(found);
        std::vector<int> tiers(kLadderRarityCount);int count=0;
        Query<Npc,Transform,Body> q{world};q.each([&](Entity,Npc& npc,Transform& at,Body& body){
            if(at.realm!=realm||npc.service!=NpcService::Assembler)return;++count;
            const auto tier=rarityIndex(npc.rarity);CHECK(tier<kLadderRarityCount);if(tier<kLadderRarityCount)++tiers[tier];
            CHECK_NEAR(body.radius,std::min(60.0,c.mobStats(npc.configIndex,npc.rarity).radius),1e-7);
            CHECK(npc.recipe<c.mob(npc.configIndex).npc.recipes.size());CHECK(!terrain.blocked(at.position,realm));
        });CHECK_EQ(count,1);for(int tier=0;tier<kLadderRarityCount;++tier)CHECK_EQ(tiers[tier],tier==rarityIndex(Rarity::Unique)?1:0);
    }
    for(const auto* id:{"assembler","cyber_assembler"}){
        double previous=0;const auto index=c.mobIndex(id);
        for(int tier=0;tier<kLadderRarityCount;++tier){const auto r=static_cast<Rarity>(tier);World w;NpcSystem sys;Terrain t;
            const auto e=sys.spawnNpc(w,t,c,index,r,{3000,3000},Realm::Overworld,0);
            CHECK(e!=NULL_ENTITY);if(e==NULL_ENTITY)continue;const double radius=w.get<Body>(e).radius;
            CHECK_NEAR(radius,std::min(60.0,c.mobStats(index,r).radius),1e-7);CHECK(radius>=previous);previous=radius;
            const auto at=w.get<Transform>(e);
            const double artRadius=c.mobStats(index,r).radius;
            CHECK_EQ(sys.findService(w,NpcService::Assembler,at.position+Vec2{artRadius+kNpcServiceReach-1,0},at.realm,kNpcServiceReach,r),e);
            CHECK_EQ(sys.findService(w,NpcService::Assembler,at.position+Vec2{artRadius+kNpcServiceReach+1,0},at.realm,kNpcServiceReach,r),NULL_ENTITY);
        }
    }
}
