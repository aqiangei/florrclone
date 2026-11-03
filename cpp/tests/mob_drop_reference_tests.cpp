#include "test.h"
#include "test_data.h"
#include "server/systems/loot.h"
#include "shared/game/drop_rates.h"
#include <map>

using namespace flix;
TEST(hyper_drop_payout_preserves_rolled_rarity_instead_of_promoting_super_to_eternal){
    for(int i=0;i<rarityIndex(Rarity::Apex);++i){const auto rarity=static_cast<Rarity>(i);CHECK_EQ(payoutDropRarity(rarity,Rarity::Apex),rarity);}
    CHECK_EQ(payoutDropRarity(Rarity::Apex,Rarity::Apex),Rarity::Unique);
    CHECK_EQ(payoutDropRarity(Rarity::Super,Rarity::Super),Rarity::Super);
}
namespace {
const ContentRegistry& registry(){static ContentRegistry c;static bool loaded=false;if(!loaded){std::string e;CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),e));loaded=true;}return c;}
using Counts=std::map<std::pair<std::string,int>,int>;
Counts kills(const char* id,Rarity rarity,int count,bool orb=false,int bonus=1){
    const auto& c=registry();World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{81927};LootSystem loot;loot.bonusMultiplier=[bonus](Entity){return bonus;};
    const Entity p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{10000,10000},0});w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100,0,0});
    if(orb){w.add<Loadout>(p);auto& s=w.get<Loadout>(p).slots[0];s.configIndex=c.petalIndex("mana_orb");s.rarity=Rarity::Mythic;}
    for(int i=0;i<count;++i){const Entity corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex(id),rarity,1});w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100,0,0});Bounty b;b.credit(p,100);w.add<Bounty>(corpse,b);w.add<Dead>(corpse,Dead{p});loot.run(w,grid,c,rng,i,0,commands,events);commands.flush();w.destroy(corpse);}
    Counts out;Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& d){out[{c.petal(d.configIndex).id,rarityIndex(d.rarity)}]+=static_cast<int>(d.quantity);});return out;
}
int total(const Counts& counts,const std::string& name){int n=0;for(const auto&[key,value]:counts)if(key.first==name)n+=value;return n;}
}
TEST(reference_pharaoh_crown_really_drops_at_unique_from_super_and_eternal_beetles){
    for(const auto tier:{Rarity::Super,Rarity::Unique}){
        const auto result=kills("pharaoh_beetle",tier,1000);
        const int crowns=total(result,"pharaoh_s_crown");CHECK(crowns>20&&crowns<110);
        for(const auto&[key,n]:result)if(key.first=="pharaoh_s_crown")CHECK_EQ(key.second,rarityIndex(Rarity::Unique));
    }
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
    CHECK_EQ(imported,76); // Reference definitions, Fire Ant Hole and two corrected body aliases
}

TEST(super_sandstorm_drops_only_sand_glass_and_stick_at_reference_tiers){
    const auto counts=kills("sandstorm",Rarity::Super,100);const int rolls=100*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(counts,"sand"),rolls);CHECK_EQ(total(counts,"glass"),rolls);CHECK_EQ(total(counts,"mysterious_stick"),rolls);
    CHECK_EQ(total(counts,"rock"),0);CHECK(counts.size()>=std::size_t(6));
    for(const auto&[key,n]:counts){CHECK(key.second==5||key.second==6||key.second==7);if(key.first=="sand"&&key.second==6)CHECK_NEAR(static_cast<double>(n)/rolls,.461*.995,.03);}
}

TEST(queen_ant_and_beetle_drop_pools_match_reference_in_actual_kills){
    const auto queen=kills("queen_ant",Rarity::Super,30);const int rolls=30*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(queen,"egg"),rolls);CHECK_EQ(total(queen,"basil"),rolls);CHECK(queen.size()>=std::size_t(4));
    const auto beetle=kills("beetle",Rarity::Super,30);
    CHECK_EQ(total(beetle,"beetle_egg"),rolls);CHECK_EQ(total(beetle,"privet_berry"),rolls);CHECK(beetle.size()>=std::size_t(4));
    const auto common=kills("queen_ant",Rarity::Common,100);CHECK_EQ(total(common,"basil"),0);
}

TEST(ant_egg_trials_and_golden_leafbug_orb_conversion_use_reference_probabilities){
    const auto common=kills("ant_egg",Rarity::Common,200);const int rolls=200*kLootFeelCopies;
    CHECK_NEAR(static_cast<double>(total(common,"egg"))/rolls,.762,.03);
    const auto super=kills("ant_egg",Rarity::Super,100);const int supers=100*kLootFeelCopies*kLootFeelSuperMultiplier;
    CHECK_EQ(total(super,"egg"),supers);CHECK_EQ(super.size(),std::size_t(3));CHECK_NEAR(static_cast<double>(super.at({"egg",5}))/supers,.357,.03);
    const auto ordinary=kills("golden_leafbug",Rarity::Super,100);CHECK_EQ(total(ordinary,"magic_leaf"),0);CHECK_NEAR(static_cast<double>(total(ordinary,"golden_leaf"))/supers,.643,.03);
    const auto magic=kills("golden_leafbug",Rarity::Super,30,true);CHECK_EQ(total(magic,"golden_leaf"),0);CHECK_EQ(total(magic,"magic_leaf"),30*kLootFeelCopies*kLootFeelSuperMultiplier);
    for(const auto&[key,n]:magic)if(key.first=="magic_leaf")CHECK_EQ(key.second,5);
}

TEST(daily_bonus_adds_independent_trials_and_reduced_super_bonus){
    const auto plain=kills("sandstorm",Rarity::Ultra,20);
    const auto twice=kills("sandstorm",Rarity::Ultra,20,false,2);
    const auto triple=kills("sandstorm",Rarity::Ultra,20,false,3);
    CHECK_EQ(total(twice,"sand"),2*total(plain,"sand"));
    CHECK_EQ(total(triple,"sand"),3*total(plain,"sand"));
    const auto base=kills("sandstorm",Rarity::Super,20);
    CHECK_EQ(total(kills("sandstorm",Rarity::Super,20,false,2),"sand"),total(base,"sand")*3/2);
    CHECK_EQ(total(kills("sandstorm",Rarity::Super,20,false,3),"sand"),total(base,"sand")*2);
}
TEST(centipede_segments_drop_leaf_and_convert_it_with_mana_orb){
    const auto normal=kills("centipede_body",Rarity::Super,30);
    CHECK(total(normal,"leaf")>0);CHECK(total(normal,"peas")>0);
    const auto magic=kills("centipede_body",Rarity::Super,30,true);
    CHECK_EQ(total(magic,"leaf"),0);CHECK(total(magic,"magic_leaf")>0);
}
TEST(super_and_eternal_actual_kills_restore_super_petals_without_quantity_inflation){
    const auto super=kills("ladybug",Rarity::Super,1000);
    const auto eternal=kills("ladybug",Rarity::Unique,1000);
    int superS=0,eternalS=0;
    for(const auto&[key,n]:super)if(key.second==7)superS+=n;
    for(const auto&[key,n]:eternal){if(key.second==7)eternalS+=n;CHECK(key.second<=7);}
    CHECK(superS>0);CHECK(eternalS>superS*2);
    CHECK_NEAR(static_cast<double>(superS)/(1000*kLootFeelCopies*2),.643*.005,.0008);
    CHECK_NEAR(static_cast<double>(eternalS)/(1000*kLootFeelCopies*2),.643*.015,.0015);
    CHECK_EQ(total(super,"light"),total(eternal,"light"));CHECK_EQ(total(super,"rose"),total(eternal,"rose"));
}
TEST(every_restored_super_table_retains_a_positive_same_tier_tail){
    Json json;std::string error;CHECK(Json::parseFile(testsupport::dataFile("mob_drops.json"),json,error));int checked=0;
    for(const auto& id:json.keys())if(json[id]["highTierPolicy"].isString())
      for(const auto& row:json[id]["drops"].items())for(const auto* key:{"rarityChances","magicRarityChances"}){
        if(row["itemType"].asString()=="splitter")continue; // Its authored rare source is deliberately unchanged.
        if(!row.contains(key))continue;DropRates rates;CHECK(parseDropRates(row[key],rates));
        double super=0,eternal=0;for(const auto& rate:ratesFor(rates,Rarity::Super))if(rate.rarity==Rarity::Super)super+=rate.probability;
        if(super<=0)continue;++checked;for(const auto& rate:ratesFor(rates,Rarity::Unique))if(rate.rarity==Rarity::Super)eternal+=rate.probability;
        CHECK_NEAR(eternal,super*3,1e-9);
      }
    CHECK(checked>=180);
}
