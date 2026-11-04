#include "test.h"
#include "test_data.h"
#include "server/systems/npcs.h"
#include <set>
using namespace flix;
namespace {
const ContentRegistry& data(){static ContentRegistry c;static bool ready=false;if(!ready){std::string e;CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),e));ready=true;}return c;}
Entity machine(World& w){Entity result=NULL_ENTITY;Query<NpcTag,Npc> q{w};q.each([&](Entity e,NpcTag&,Npc& n){if(n.service==NpcService::Assembler){CHECK(result==NULL_ENTITY);result=e;}});return result;}
}
TEST(assembler_recipe_pool_contains_all_reference_recipes_and_fragment_sawblade){
    const auto& c=data();const auto& config=c.mob(c.mobIndex("assembler"));
    CHECK_EQ(config.npc.recipes.size(),13u);int moonRecipes=0;bool fragment=false,pentagon=false;
    for(const auto& recipe:config.npc.recipes){
        bool moon=false;
        for(const auto& item:recipe.inputs){CHECK(item.petal!=kInvalidIndex);moon=moon||c.petal(item.petal).id=="moon_rock";}
        for(const auto& item:recipe.outputs){CHECK(item.petal!=kInvalidIndex);moon=moon||c.petal(item.petal).id=="moon_rock";}
        moonRecipes+=moon;
        for(const auto& item:recipe.outputs){
            pentagon=pentagon||c.petal(item.petal).id=="pentagon";
            if(c.petal(item.petal).id=="sawblade")for(const auto& input:recipe.inputs)
                fragment=fragment||c.petal(input.petal).id=="fragment";
        }
    }
    CHECK_EQ(moonRecipes,2);
    CHECK(fragment);CHECK(pentagon);
    const auto& saw=config.npc.recipes.back();
    CHECK_EQ(saw.takes(c.petalIndex("cogwheel")),1u);
    CHECK_EQ(saw.takes(c.petalIndex("fragment")),20u);
}
TEST(assembler_refreshes_after_twenty_minutes_with_a_new_recipe_and_network_id){
    World w;Terrain terrain;NpcSystem npcs;NetIdAllocator ids;npcs.netIds=&ids;
    auto e=npcs.spawnNpc(w,terrain,data(),data().mobIndex("assembler"),Rarity::Super,{1000,1000},Realm::Overworld,1000);
    const auto first=w.get<Npc>(e).recipe;const auto netId=w.get<NetId>(e).value;
    npcs.run(w,terrain,data(),{},1000+kAssemblerRecipeRefreshMillis-1);CHECK(w.isAlive(e));
    npcs.run(w,terrain,data(),{},1000+kAssemblerRecipeRefreshMillis);CHECK(!w.isAlive(e));
    const auto current=machine(w);CHECK(current!=NULL_ENTITY);
    CHECK(w.get<Npc>(current).recipe!=first);CHECK(w.get<NetId>(current).value!=netId);
    CHECK(w.get<Npc>(current).rarity==Rarity::Super);
}
TEST(assembler_refreshes_while_players_wait_and_follows_its_announced_next_recipe){
    World w;Terrain terrain;NpcSystem npcs;
    const auto e=npcs.spawnNpc(w,terrain,data(),data().mobIndex("assembler"),Rarity::Super,{1000,1000},Realm::Overworld,0);
    const auto at=w.get<Transform>(e);const auto next=w.get<Npc>(e).nextRecipe;
    CHECK_NEAR(w.get<Npc>(e).recipeRefreshAtMillis,kAssemblerRecipeRefreshMillis,1e-8);
    npcs.run(w,terrain,data(),{{at.position,at.realm}},kAssemblerRecipeRefreshMillis);
    CHECK(!w.isAlive(e));
    const auto current=machine(w);
    CHECK_EQ(w.get<Npc>(current).recipe,next);
    CHECK_NEAR(w.get<Npc>(current).recipeRefreshAtMillis,2*kAssemblerRecipeRefreshMillis,1e-8);
}
TEST(assembler_rotation_visits_every_available_recipe_without_random_starvation){
    World w;Terrain terrain;NpcSystem npcs;
    auto e=npcs.spawnNpc(w,terrain,data(),data().mobIndex("assembler"),Rarity::Super,{1000,1000},Realm::Overworld,0);
    std::set<unsigned> seen;
    for(int i=0;i<13;++i){seen.insert(w.get<Npc>(e).recipe);npcs.run(w,terrain,data(),{},(i+1)*kAssemblerRecipeRefreshMillis);e=machine(w);}
    CHECK_EQ(seen.size(),13u);
}
TEST(assembler_rotation_does_not_offer_petals_missing_at_the_machine_rarity){
    for(int tier=0;tier<kLadderRarityCount;++tier){
        World w;Terrain terrain;NpcSystem npcs;
        auto e=npcs.spawnNpc(w,terrain,data(),data().mobIndex("assembler"),clampRarity(tier),{1000,1000},Realm::Overworld,0);
        for(int cycle=0;cycle<20;++cycle){
            const auto& npc=w.get<Npc>(e);const auto& recipe=data().mob(npc.configIndex).npc.recipes[npc.recipe];
            for(const auto& item:recipe.inputs)CHECK(petalComesAt(data().petal(item.petal),npc.rarity));
            for(const auto& item:recipe.outputs)CHECK(petalComesAt(data().petal(item.petal),npc.rarity));
            const auto old=npc.recipe;
            npcs.run(w,terrain,data(),{},(cycle+1)*kAssemblerRecipeRefreshMillis);
            e=machine(w);CHECK(w.get<Npc>(e).recipe!=old);
        }
    }
}
