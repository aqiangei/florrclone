#include "test.h"
#include "test_data.h"
#include "server/systems/loot.h"
#include "shared/game/drop_rates.h"
#include <map>

using namespace flix;
namespace {
const ContentRegistry& registry(){static ContentRegistry c;static bool loaded=false;if(!loaded){std::string e;CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),e));loaded=true;}return c;}
using Counts=std::map<std::pair<std::string,int>,int>;
Counts kills(const char* id,Rarity rarity,int count,bool orb=false){
    const auto& c=registry();World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{81927};LootSystem loot;
    const Entity p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{10000,10000},0});w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100,0,0});
    if(orb){w.add<Loadout>(p);auto& s=w.get<Loadout>(p).slots[0];s.configIndex=c.petalIndex("mana_orb");s.rarity=Rarity::Mythic;}
    for(int i=0;i<count;++i){const Entity corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex(id),rarity,1});w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100,0,0});Bounty b;b.credit(p,100);w.add<Bounty>(corpse,b);w.add<Dead>(corpse,Dead{p});loot.run(w,grid,c,rng,i,0,commands,events);commands.flush();w.destroy(corpse);}
    Counts out;Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& d){out[{c.petal(d.configIndex).id,rarityIndex(d.rarity)}]+=static_cast<int>(d.quantity);});return out;
}
int total(const Counts& counts,const std::string& name){int n=0;for(const auto&[key,value]:counts)if(key.first==name)n+=value;return n;}
}

TEST(reference_drop_cdf_preserves_no_drop_and_clips_rounding_without_renormalizing){
    Json json;std::string error;CHECK(Json::parse("[[[0,59.8],[1,16.4]],[[0,60],[1,40.1]]]",json,error));DropRates rates;CHECK(parseDropRates(json,rates));
    CHECK_EQ(rollDropRates(rates,Rarity::Common,.597999),0);CHECK_EQ(rollDropRates(rates,Rarity::Common,.598),1);CHECK_EQ(rollDropRates(rates,Rarity::Common,.762),-1);
    CHECK_EQ(rollDropRates(rates,Rarity::Super,.999999),1);CHECK_NEAR(rates[1][1].probability,.4,1e-12);
}

TEST(all_imported_mob_tables_link_without_obsolete_generated_eggs){
    const auto& c=registry();DropTables tables;std::string error;CHECK(tables.load(c,testsupport::dataFile("mob_drops.json"),error));CHECK(tables.unresolved().empty());
    Json json;CHECK(Json::parseFile(testsupport::dataFile("mob_drops.json"),json,error));int imported=0;
    for(const auto& id:json.keys())if(json[id].contains("referenceMob")){
        ++imported;const auto& entries=tables.guaranteedForMob(c.mobIndex(id));CHECK_EQ(entries.size(),json[id]["drops"].size());
        for(const auto& e:entries){CHECK(e.petalIndex!=kNoPetal);CHECK(!c.petal(e.petalIndex).isAdminPetal);CHECK(!e.rates.empty());for(int tier=0;tier<kLadderRarityCount;++tier){double sum=0;for(const auto&r:ratesFor(e.rates,static_cast<Rarity>(tier)))sum+=r.probability;CHECK(sum<=1.0000000001);}}
    }
    CHECK_EQ(imported,74); // 73 reference definitions plus Fire Ant Hole alias
}

TEST(super_sandstorm_drops_only_sand_glass_and_stick_at_reference_tiers){
    const auto counts=kills("sandstorm",Rarity::Super,100);const int rolls=100*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(counts,"sand"),rolls);CHECK_EQ(total(counts,"glass"),rolls);CHECK_EQ(total(counts,"mysterious_stick"),rolls);
    CHECK_EQ(total(counts,"rock"),0);CHECK_EQ(counts.size(),std::size_t(6));
    for(const auto&[key,n]:counts){CHECK(key.second==5||key.second==6);if(key.first=="sand"&&key.second==6)CHECK_NEAR(static_cast<double>(n)/rolls,.461,.03);}
}

TEST(queen_ant_and_beetle_drop_pools_match_reference_in_actual_kills){
    const auto queen=kills("queen_ant",Rarity::Super,30);const int rolls=30*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(queen,"egg"),rolls);CHECK_EQ(total(queen,"basil"),rolls);CHECK_EQ(queen.size(),std::size_t(4));
    const auto beetle=kills("beetle",Rarity::Super,30);
    CHECK_EQ(total(beetle,"beetle_egg"),rolls);CHECK_EQ(total(beetle,"privet_berry"),rolls);CHECK_EQ(beetle.size(),std::size_t(4));
    const auto common=kills("queen_ant",Rarity::Common,100);CHECK_EQ(total(common,"basil"),0);
}

TEST(ant_egg_trials_and_golden_leafbug_orb_conversion_use_reference_probabilities){
    const auto common=kills("ant_egg",Rarity::Common,200);const int rolls=200*kLootFeelCopies;
    CHECK_NEAR(static_cast<double>(total(common,"egg"))/rolls,.762,.03);
    const auto super=kills("ant_egg",Rarity::Super,100);const int supers=100*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(super,"egg"),supers);CHECK_EQ(super.size(),std::size_t(2));CHECK_NEAR(static_cast<double>(super.at({"egg",5}))/supers,.357,.03);
    const auto ordinary=kills("golden_leafbug",Rarity::Super,100);CHECK_EQ(total(ordinary,"magic_leaf"),0);CHECK_NEAR(static_cast<double>(total(ordinary,"golden_leaf"))/supers,.643,.03);
    const auto magic=kills("golden_leafbug",Rarity::Super,30,true);CHECK_EQ(total(magic,"golden_leaf"),0);CHECK_EQ(total(magic,"magic_leaf"),30*kLootFeelCopies*kLootFeelSuperMultiplier);
    for(const auto&[key,n]:magic)if(key.first=="magic_leaf")CHECK_EQ(key.second,5);
}
