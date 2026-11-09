#include "test.h"
#include "test_data.h"
#include "server/systems/loot.h"
#include "server/systems/spawning.h"
#include "server/systems/combat.h"
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
Counts kills(const char* id,Rarity rarity,int count,bool orb=false,int bonus=1,int* maxEternal=nullptr,bool splitAccount=false){
    const auto& c=registry();World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{81927};LootSystem loot;loot.bonusMultiplier=[bonus](Entity){return bonus;};
    const Entity p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{10000,10000},0});w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100,0,0});
    w.add<PlayerAccount>(p,PlayerAccount{"audit-user","audit",77,false});
    Entity sibling=NULL_ENTITY;
    if(splitAccount){sibling=w.create();w.add<PlayerTag>(sibling);w.add<PlayerAccount>(sibling,PlayerAccount{"audit-user","audit",77,false});w.add<Transform>(sibling,Transform{{10000,10000},0});w.add<Health>(sibling,Health{100,100,0,0});}
    int previousEternal=0;
    if(orb){w.add<Loadout>(p);auto& s=w.get<Loadout>(p).slots[0];s.configIndex=c.petalIndex("mana_orb");s.rarity=Rarity::Mythic;}
    for(int i=0;i<count;++i){const Entity corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex(id),rarity,1});w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100,0,0});Bounty b;b.credit(p,100,77);if(splitAccount)b.credit(sibling,100,77);w.add<Bounty>(corpse,b);w.add<Dead>(corpse,Dead{p});loot.run(w,grid,c,rng,i,0,commands,events);commands.flush();w.destroy(corpse);
        if(maxEternal){int now=0;Query<DropTag,DropItem> q{w};q.each([&](Entity,DropTag&,DropItem& d){if(d.rarity==Rarity::Unique&&LootSystem::mayPickUp(d,p,77))now+=static_cast<int>(d.quantity);});*maxEternal=std::max(*maxEternal,now-previousEternal);previousEternal=now;}
    }
    Counts out;Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& d){out[{c.petal(d.configIndex).id,rarityIndex(d.rarity)}]+=static_cast<int>(d.quantity);});return out;
}
int total(const Counts& counts,const std::string& name){int n=0;for(const auto&[key,value]:counts)if(key.first==name)n+=value;return n;}
}
double expectedHyperEternals(const char* id) {
    const auto& c=registry();DropTables tables;tables.link(c);std::vector<HyperEternalDropWeight> weights;
    for(const auto& e:tables.guaranteedForMob(c.mobIndex(id)))
        if(e.petalIndex!=kInvalidIndex&&petalComesAt(c.petal(e.petalIndex),Rarity::Unique))weights.push_back(hyperEternalDropWeight(e.rates,c.petal(e.petalIndex).minRarity));
    double expected=0;for(double chance:hyperEternalChances(weights,kHyperEternalDropBudget/kLootFeelCopies))expected+=chance*kLootFeelCopies;
    return expected;
}
TEST(hyper_drop_budget_is_shared_across_rows_and_scales_for_the_top_contributor){
    for(const auto id:{"ladybug","bee","beetle"}){
        const auto result=kills(id,Rarity::Apex,2000);int eternal=0;
        for(const auto& [key,n]:result)if(key.second==rarityIndex(Rarity::Unique))eternal+=n;
        CHECK_NEAR(eternal/2000.0,expectedHyperEternals(id)*1.3,0.08);
    }
}
TEST(hyper_drop_budget_bonus_uses_one_point_four_and_one_point_eight_rates){
    for(int bonus=2;bonus<=3;++bonus){
        const auto result=kills("ladybug",Rarity::Apex,2000,false,bonus);int eternal=0;
        for(const auto& [key,n]:result)if(key.second==rarityIndex(Rarity::Unique))eternal+=n;
        CHECK_NEAR(eternal/2000.0,expectedHyperEternals("ladybug")*1.3*(bonus==2?1.4:1.8),0.1);
    }
}
TEST(reference_pharaoh_crown_drops_at_eternal_from_super_eternal_and_hyper_beetles){
    for(const auto tier:{Rarity::Super,Rarity::Unique,Rarity::Apex}){
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
    CHECK_EQ(imported,77); // Current imported definitions and corrected body aliases
}

TEST(super_sandstorm_drops_only_sand_glass_and_stick_at_reference_tiers){
    const auto counts=kills("sandstorm",Rarity::Super,100);const int rolls=100*baseLootTrialsForRarity(Rarity::Super);
    CHECK_EQ(total(counts,"sand"),rolls);CHECK_EQ(total(counts,"glass"),rolls);CHECK_EQ(total(counts,"mysterious_stick"),rolls);
    CHECK_EQ(total(counts,"rock"),0);CHECK(counts.size()>=std::size_t(6));
    for(const auto&[key,n]:counts){CHECK(key.second==5||key.second==6||key.second==7);if(key.first=="sand"&&key.second==6)CHECK_NEAR(static_cast<double>(n)/rolls,.461*.995,.03);}
}

TEST(queen_ant_and_beetle_drop_pools_match_reference_in_actual_kills){
    const auto queen=kills("queen_ant",Rarity::Super,30);const int rolls=30*baseLootTrialsForRarity(Rarity::Super);
    CHECK_EQ(total(queen,"egg"),rolls);CHECK_EQ(total(queen,"basil"),rolls);CHECK(queen.size()>=std::size_t(4));
    const auto beetle=kills("beetle",Rarity::Super,30);
    CHECK_EQ(total(beetle,"beetle_egg"),rolls);CHECK_EQ(total(beetle,"privet_berry"),rolls);CHECK(beetle.size()>=std::size_t(4));
    const auto common=kills("queen_ant",Rarity::Common,100);CHECK_EQ(total(common,"basil"),0);
}

TEST(ant_egg_trials_and_golden_leafbug_orb_conversion_use_reference_probabilities){
    const auto common=kills("ant_egg",Rarity::Common,200);const int rolls=200*kLootFeelCopies;
    CHECK_NEAR(static_cast<double>(total(common,"egg"))/rolls,.762,.03);
    const auto super=kills("ant_egg",Rarity::Super,100);const int supers=100*baseLootTrialsForRarity(Rarity::Super);
    CHECK_EQ(total(super,"egg"),supers);CHECK_EQ(super.size(),std::size_t(3));CHECK_NEAR(static_cast<double>(super.at({"egg",5}))/supers,.357,.03);
    const auto ordinary=kills("golden_leafbug",Rarity::Super,100);CHECK_EQ(total(ordinary,"magic_leaf"),0);CHECK_NEAR(static_cast<double>(total(ordinary,"golden_leaf"))/supers,.643,.03);
    const auto magic=kills("golden_leafbug",Rarity::Super,30,true);CHECK_EQ(total(magic,"golden_leaf"),0);CHECK_EQ(total(magic,"magic_leaf"),30*baseLootTrialsForRarity(Rarity::Super));
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
    const auto eternal=kills("sandstorm",Rarity::Unique,20);
    CHECK_EQ(total(kills("sandstorm",Rarity::Unique,20,false,2),"sand")*10,total(eternal,"sand")*16);
    CHECK_EQ(total(kills("sandstorm",Rarity::Unique,20,false,3),"sand")*10,total(eternal,"sand")*22);
}
TEST(daily_bonus_mixed_claimants_keep_only_their_own_super_eternal_and_hyper_trials){
    const auto& c=registry();
    for(const auto rarity:{Rarity::Super,Rarity::Unique,Rarity::Apex}) {
        World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{81927};LootSystem loot;
        std::vector<Entity> players;
        for(int i=0;i<3;++i){const auto p=w.create();players.push_back(p);w.add<PlayerTag>(p);
            w.add<Transform>(p,Transform{{10000,10000},0});w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100});
            w.add<PlayerAccount>(p,PlayerAccount{"bonus-"+std::to_string(i),"bonus",static_cast<std::uint32_t>(70+i),false});}
        loot.bonusMultiplier=[&](Entity p){for(int i=0;i<3;++i)if(players[i]==p)return i+1;return 1;};
        const auto corpse=w.create();w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex("sandstorm"),rarity,1});
        w.add<Transform>(corpse,Transform{{1000,1000},0});w.add<Health>(corpse,Health{0,100});Bounty bounty;
        for(int i=0;i<3;++i)bounty.credit(players[i],100,70+i);
        w.add<Bounty>(corpse,bounty);w.add<Dead>(corpse,Dead{players[0]});
        loot.run(w,grid,c,rng,1000,0,commands,events);commands.flush();
        int counts[3]={};Query<DropTag,DropItem> drops{w};
        drops.each([&](Entity,DropTag&,DropItem& item){if(c.petal(item.configIndex).id!="sand")return;
            for(int i=0;i<3;++i)if(LootSystem::mayPickUp(item,players[i],70+i))counts[i]+=static_cast<int>(item.quantity);});
        for(int i=0;i<3;++i) {
            const int expected = bonusLootTrialsForRarity(rarity,i+1);
            CHECK_EQ(counts[i], rarity == Rarity::Apex && i == 0 ? expected * 13 / 10 : expected);
        }
    }
}

TEST(hyper_rewards_follow_personal_damage_and_km_wins_ties_before_the_finisher) {
    const auto& content = registry();
    for (const double maxHealth : {100000.0, 1000000.0}) {
        World world;
        CommandBuffer commands{world};
        EventQueue events;
        SpatialGrid grid;
        Rng rng{8157};
        LootSystem loot;
        CombatSystem combat;
        std::array<Entity, 4> players{};
        for (Entity& player : players) {
            player = world.create();
            world.add<PlayerTag>(player);
            world.add<Health>(player, Health{100, 100});
            world.add<Transform>(player, Transform{{10000, 10000}, 0});
            world.add<Faction>(player, Faction{Team::Players});
            world.add<PlayerProgress>(player);
        }

        constexpr int kills = 100;
        for (int i = 0; i < kills; ++i) {
            const Entity corpse = world.create();
            world.add<MobTag>(corpse);
            world.add<MobType>(corpse, MobType{content.mobIndex("sandstorm"), Rarity::Apex, 1});
            world.add<Transform>(corpse, Transform{{1000, 1000}, 0});
            world.add<Faction>(corpse, Faction{Team::Hostiles});
            world.add<Health>(corpse, Health{2999, maxHealth});
            world.add<Bounty>(corpse).xp = 1000;
            const double now = i * 100.0 + 1000.0;
            combat.applyDamage(world, corpse, players[0], 1000, now);
            combat.applyDamage(world, corpse, players[1], 1000, now);
            CHECK(combat.applyDamage(world, corpse, players[2], 999, now).killed);
            loot.run(world, grid, content, rng, now, 0.0, commands, events);
            commands.flush();
            world.destroy(corpse);
        }

        std::array<int, 4> sand{};
        Query<DropTag, DropItem>{world}.each([&](Entity, DropTag&, DropItem& item) {
            if (content.petal(item.configIndex).id != "sand" || item.rarity == Rarity::Unique)
                return;
            for (std::size_t i = 0; i < players.size(); ++i)
                if (LootSystem::mayPickUp(item, players[i], 0)) sand[i] += item.quantity;
        });
        const bool secondMeetsFloor = maxHealth == 100000.0;
        CHECK_EQ(sand[0], kills * 39);
        CHECK_NEAR(sand[1], kills * (secondMeetsFloor ? 30 : 15), secondMeetsFloor ? 0 : 100);
        CHECK_NEAR(sand[2], kills * 15, 100);
        CHECK_EQ(sand[3], 0);
        CHECK_EQ(world.get<PlayerProgress>(players[0]).totalXp, kills * 1300.0);
        CHECK_EQ(world.get<PlayerProgress>(players[1]).totalXp,
                 kills * (secondMeetsFloor ? 1000.0 : 500.0));
        CHECK_EQ(world.get<PlayerProgress>(players[2]).totalXp, kills * 500.0);
        CHECK_EQ(world.get<PlayerProgress>(players[3]).totalXp, 0.0);
    }
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
    CHECK_NEAR(static_cast<double>(superS)/(1000*baseLootTrialsForRarity(Rarity::Super)*2),.643*.005,.0008);
    CHECK_NEAR(static_cast<double>(eternalS)/(1000*kLootFeelCopies*2),.643*.015,.0015);
    CHECK_EQ(total(super,"light")*3,total(eternal,"light")*2);CHECK_EQ(total(super,"rose")*3,total(eternal,"rose")*2);
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

TEST(computer_hyper_actual_kills_share_one_eternal_budget_and_standard_super_tails){
    for(const char* id:{"javascript","c++","dust","glitch","glitch_flower","cube","sun","server","mecha_flower","mecha_wasp","mecha_spider","mecha_crab","barrel"})
    for(int bonus=1;bonus<=3;++bonus){
        int maximum=0;const auto result=kills(id,Rarity::Apex,300,false,bonus,&maximum);int eternal=0,super=0;
        for(const auto&[key,n]:result){if(key.second==8)eternal+=n;if(key.second==7)super+=n;CHECK(key.second<=8);}
        const double trialScale = static_cast<double>(bonusLootTrialsForRarity(Rarity::Apex,bonus)) /
            baseLootTrialsForRarity(Rarity::Apex);
        CHECK_NEAR(eternal/300.0,expectedHyperEternals(id)*1.3*trialScale,.18);
        CHECK(super/300.0<4.0); // Previously glitch 60, glitch_flower 107 at base bonus.
        CHECK(maximum<10);
        std::printf("  COMPUTER_H %s bonus=%d meanE=%.4f meanS=%.4f maxE=%d\n",id,bonus,eternal/300.0,super/300.0,maximum);
    }
}
TEST(computer_hyper_soul_split_account_does_not_duplicate_eternal_entitlement){
    const auto ordinary=kills("server",Rarity::Apex,300);const auto split=kills("server",Rarity::Apex,300,false,1,nullptr,true);
    CHECK(ordinary==split);
}

TEST(all_three_ladybugs_convert_to_mana_orbs_only_for_orb_wearers){
 for(const char* mob:{"ladybug","jungle_ladybug","desert_ladybug"}){
  CHECK_EQ(total(kills(mob,Rarity::Apex,80,false),"mana_orb"),0);
  const auto withOrb=kills(mob,Rarity::Apex,80,true);
  CHECK(total(withOrb,"mana_orb")>0);
  for(const auto& [key,n]:withOrb)if(key.first=="mana_orb")CHECK(key.second<=5);
  // The separate H->E pool must convert too, not leak ordinary source petals.
  CHECK_EQ(total(withOrb,std::string(mob)=="jungle_ladybug"?"dahlia":"rose"),0);
 }
}

TEST(reported_hyper_screen_and_sandstorm_keep_split_account_loot) {
    for (const char* id : {"server", "sandstorm"}) {
        const auto ordinary = kills(id, Rarity::Apex, 300);
        const auto split = kills(id, Rarity::Apex, 300, false, 1, nullptr, true);
        CHECK(ordinary == split);
        int normal = 0;
        for (const auto& [key, count] : split) if (key.second < rarityIndex(Rarity::Unique)) normal += count;
        CHECK(normal >= 300);
        std::printf("  SPLIT_H %s normal=%d across 300 kills\n", id, normal);
        // A single qualifying kill is not an empty drop just because its
        // independent eternal roll failed.
        for (bool splitBody : {false, true}) {
            const auto single = kills(id, Rarity::Apex, 1, false, 1, nullptr, splitBody);
            CHECK(!single.empty());
        }
    }
}

TEST(hyper_hammer_buff_preserves_reload_and_temporary_eternal_hammer) {
    const auto& c = registry();
    const auto hammer = c.petalIndex("mjolnir");
    const auto timed = c.petalIndex("timed_mjolnir");
    CHECK(hammer != kInvalidIndex); CHECK(timed != kInvalidIndex);
    if (hammer == kInvalidIndex || timed == kInvalidIndex) return;
    const auto h = c.petalStats(hammer, Rarity::Apex);
    const auto e = c.petalStats(hammer, Rarity::Unique);
    const auto temporary = c.petalStats(timed, Rarity::Unique);
    CHECK_NEAR(h.lightningDamagePerHit, 60 * petalStrengthScale(Rarity::Apex), 1e-8);
    CHECK_NEAR(h.health, e.health * 2.25, 1e-8);
    CHECK_NEAR(h.reloadMillis, 6000, 1e-8);
    CHECK_EQ(h.lightningBounces, 0);
    CHECK_NEAR(temporary.lightningDamagePerHit, 12 * petalStrengthScale(Rarity::Unique), 1e-8);
    CHECK_NEAR(temporary.reloadMillis, 8000, 1e-8);
}

TEST(carrot_projectile_speed_keeps_range_and_reload) {
    const auto& c=registry();const auto index=c.petalIndex("carrot");
    CHECK(index!=kInvalidIndex);if(index==kInvalidIndex)return;
    CHECK_NEAR(c.petal(index).projectile.speed,450,1e-9);
    CHECK_NEAR(c.petal(index).projectile.distance,500,1e-9);
    for(int tier=0;tier<kLadderRarityCount;++tier)
        CHECK_NEAR(c.petalStats(index,static_cast<Rarity>(tier)).reloadMillis,1000,1e-9);
}

TEST(centipede_body_kills_award_loot_without_soul_splitter) {
    const auto& c=registry();
    for(const char* name:{"hel_centipede","jungle_centipede","centipede","desert_centipede","evil_centipede"})
    for(const auto rarity:{Rarity::Super,Rarity::Unique,Rarity::Apex}) {
        World w;Terrain terrain;SpawnSystem spawn;CombatSystem combat;LootSystem loot;
        CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{91};
        const auto player=w.create();w.add<PlayerTag>(player);w.add<PlayerAccount>(player,PlayerAccount{"body-loot","body-loot",77,false});
        w.add<Health>(player,Health{100,100});w.add<Transform>(player,Transform{{10000,10000},0});w.add<Faction>(player,Faction{Team::Players});
        const auto head=spawn.spawnMob(w,terrain,c,c.mobIndex(name),rarity,{3000,3000},Realm::Overworld,0,rng);
        CHECK(head!=NULL_ENTITY);if(head==NULL_ENTITY)continue;
        const auto body=w.get<BodySegment>(head).behind;CHECK(body!=NULL_ENTITY);if(body==NULL_ENTITY)continue;
        CHECK(!w.get<BodySegment>(body).sharedHealth);
        const auto hit=combat.applyDamage(w,body,player,w.get<Health>(body).max*2,1000);
        CHECK(hit.killed);CHECK(w.has<Dead>(body));CHECK(!w.has<Dead>(head));
        CHECK(!w.get<Bounty>(body).contributors.empty());
        loot.run(w,grid,c,rng,1000,net::kTickSeconds,commands,events);commands.flush();
        std::uint32_t quantity=0;Query<DropTag,DropItem> drops{w};drops.each([&](Entity,DropTag&,DropItem& drop){if(LootSystem::mayPickUp(drop,player,77))quantity+=drop.quantity;});
        CHECK(quantity>0);
    }
}

TEST(hyper_eternal_allocations_do_not_exceed_super_chances) {
    const auto& c=registry();DropTables tables;tables.link(c);
    for(std::uint16_t mob=0;mob<c.mobCount();++mob){
        std::vector<HyperEternalDropWeight> weights;
        for(const auto& row:tables.guaranteedForMob(mob))
            if(row.petalIndex!=kInvalidIndex&&petalComesAt(c.petal(row.petalIndex),Rarity::Unique))
                weights.push_back(hyperEternalDropWeight(row.rates,c.petal(row.petalIndex).minRarity));
        const auto chances=hyperEternalChances(weights,kHyperEternalDropBudget/kLootFeelCopies);
        double sum = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            sum += chances[i];
            if (weights[i].superChance >= 0.0)
                CHECK(chances[i] <= weights[i].superChance * 0.5 + weights[i].eternalOnlyChance + 1e-12);
        }
        CHECK(sum<=kHyperEternalDropBudget/kLootFeelCopies+1e-12);
    }
    const auto result=kills("mecha_flower",Rarity::Apex,4000);
    for(const char* id:{"battery","cogwheel","fragment","mimic"}){
        auto number=[&](Rarity rarity){auto i=result.find({id,rarityIndex(rarity)});return i==result.end()?0:i->second;};
        CHECK(number(Rarity::Super)>number(Rarity::Unique));
    }
}
