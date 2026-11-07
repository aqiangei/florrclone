#include "test.h"
#include "test_data.h"
#include "server/systems/loot.h"
#include "shared/game/garbage_contents.h"
#include <map>
#include <set>
#include <filesystem>
using namespace flix;
namespace {
const ContentRegistry& garbageRegistry(){static ContentRegistry c;static bool loaded=false;if(!loaded){std::string e;CHECK(c.load(testsupport::dataDir(),e));loaded=true;}return c;}
struct GarbageKill {std::vector<GarbagePetal> visible;std::vector<std::map<std::uint16_t,unsigned>> loot;unsigned entities=0;};
GarbageKill openBag(const ContentRegistry& c,Rarity rarity,std::uint32_t seed,int people=1,int bonus=1,bool orb=false){
    World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;Rng rng{713+seed};LootSystem loot;loot.bonusMultiplier=[bonus](Entity){return bonus;};
    std::vector<Entity> players;
    for(int i=0;i<people;++i){const auto p=w.create();players.push_back(p);w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{10000.0+i*100,10000},0});w.add<Health>(p,Health{100,100});w.add<Body>(p,Body{25,1});if(orb){w.add<Loadout>(p);w.get<Loadout>(p).slots[0].configIndex=c.petalIndex("mana_orb");w.get<Loadout>(p).slots[0].rarity=Rarity::Common;}}
    const auto corpse=w.create();const Vec2 at{1000,1000};w.add<MobTag>(corpse);w.add<MobType>(corpse,MobType{c.mobIndex("garbage"),rarity,1});w.add<Transform>(corpse,Transform{at,0});w.add<Health>(corpse,Health{0,100});w.add<NetId>(corpse,NetId{seed});
    Bounty bounty;for(auto p:players)bounty.credit(p,100);w.add<Bounty>(corpse,bounty);w.add<Dead>(corpse,Dead{players.front()});
    GarbageKill result;result.visible=garbageContents(garbageEligiblePetals(c,rarity),seed,at,&c);result.loot.resize(players.size());
    loot.run(w,grid,c,rng,1000,0,commands,events);commands.flush();
    auto gather=[&](){std::vector<std::map<std::uint16_t,unsigned>> found(players.size());Query<DropTag,DropItem> drops{w};result.entities=0;drops.each([&](Entity,DropTag&,DropItem& item){++result.entities;CHECK(petalComesAt(c.petal(item.configIndex),item.rarity));CHECK(!c.petal(item.configIndex).isAdminPetal);for(std::size_t i=0;i<players.size();++i)if(LootSystem::mayPickUp(item,players[i],0))found[i][item.configIndex]+=item.quantity;});return found;};
    result.loot=gather();loot.run(w,grid,c,rng,1100,0,commands,events);commands.flush();CHECK(gather()==result.loot);
    return result;
}
}
TEST(garbage_preview_is_stable_unique_and_covers_playable_types){
    const auto& c=garbageRegistry();std::set<std::uint16_t> seen;
    const auto pool=garbageEligiblePetals(c,Rarity::Super);CHECK(pool.size()>50);
    for(unsigned seed=1;seed<=2000;++seed){const auto a=garbageContents(pool,seed,{10,20},&c),b=garbageContents(pool,seed,{9000,8000},&c);CHECK(a.size()>=5&&a.size()<=9);CHECK_EQ(a.size(),b.size());std::set<std::uint16_t> unique;for(std::size_t i=0;i<a.size();++i){CHECK_EQ(a[i].index,b[i].index);CHECK_EQ(a[i].poseSeed,b[i].poseSeed);unique.insert(a[i].index);seen.insert(a[i].index);}CHECK_EQ(unique.size(),a.size());}
    CHECK_EQ(seen.size(),pool.size());CHECK(seen.count(c.petalIndex("leaf")));CHECK(seen.count(c.petalIndex("mana_orb")));CHECK(seen.count(c.petalIndex("splitter")));CHECK(!seen.count(c.petalIndex("timed_mjolnir")));
}

TEST(garbage_cpp_is_rarer_but_visible_cpp_still_really_drops) {
    const auto& c=garbageRegistry();
    const auto cpp=c.petalIndex("c++"); const auto pool=garbageEligiblePetals(c,Rarity::Super);
    int before=0,after=0; unsigned visibleSeed=0,hiddenSeed=0;
    for(unsigned seed=1;seed<=50000;++seed) {
        const auto old=garbageContents(pool,seed,{1000,1000},nullptr);
        const auto now=garbageContents(pool,seed,{1000,1000},&c);
        const auto has=[&](const auto& items){return std::any_of(items.begin(),items.end(),[&](const auto& item){return item.index==cpp;});};
        before+=has(old); after+=has(now);
        if(has(now))visibleSeed=seed; else if(has(old))hiddenSeed=seed;
    }
    CHECK(before>100); CHECK(after>0);
    CHECK(after>before*.55 && after<before*.65);
    for(int bonus:{1,2,3}) {
        auto shown=openBag(c,Rarity::Super,visibleSeed,1,bonus);
        auto hidden=openBag(c,Rarity::Super,hiddenSeed,1,bonus);
        CHECK(shown.loot[0][cpp]>0); CHECK_EQ(hidden.loot[0][cpp],0u);
    }
    for(auto tier:{Rarity::Ultra,Rarity::Super,Rarity::Unique,Rarity::Apex}) {
        const auto a=c.petalStats(cpp,tier), b=c.petalStats(c.petalIndex("sawblade"),tier);
        CHECK(a.damage>=b.damage && a.damage<=b.damage*1.06);
        CHECK(a.health>=b.health && a.health<=b.health*1.13);
        CHECK_NEAR(a.reloadMillis,b.reloadMillis,1e-9);
    }
}
TEST(todo1107_garbage_rare_items_remain_visible_but_less_frequent) {
    const auto& c = garbageRegistry();
    const auto pool = garbageEligiblePetals(c, Rarity::Super);
    std::array<int, 4> before{}, after{};
    const std::array ids{c.petalIndex("mimic"), c.petalIndex("sawblade"),
                         c.petalIndex("golden_leaf"), c.petalIndex("ruby")};
    for (unsigned seed = 1; seed <= 50000; ++seed) {
        const auto original = garbageContents(pool, seed, {1000, 1000}, nullptr);
        const auto adjusted = garbageContents(pool, seed, {1000, 1000}, &c);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto matches = [&](const GarbagePetal& petal) { return petal.index == ids[i]; };
            before[i] += std::any_of(original.begin(), original.end(), matches);
            after[i] += std::any_of(adjusted.begin(), adjusted.end(), matches);
        }
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(after[i] > before[i] * 0.77);
        CHECK(after[i] < before[i] * 0.93);
    }
}

TEST(garbage_every_visible_type_drops_and_no_hidden_types_appear_with_or_without_orbs){
    const auto& c=garbageRegistry();
    for(const auto rarity:{Rarity::Common,Rarity::Ultra,Rarity::Super,Rarity::Unique})for(const bool orb:{false,true})for(unsigned seed=1;seed<=8;++seed){
        const auto result=openBag(c,rarity,seed,rarity==Rarity::Common?1:2,1,orb);std::set<std::uint16_t> expected;for(const auto& p:result.visible)expected.insert(p.index);
        for(const auto& player:result.loot){CHECK_EQ(player.size(),expected.size());for(const auto&[id,n]:player){CHECK(expected.count(id));CHECK(n>=1);}}
        CHECK(result.entities<=result.visible.size()*3);
    }
}
TEST(garbage_bonus_adds_trials_without_changing_preview_or_removing_guarantees){
    const auto& c=garbageRegistry();
    for(const auto rarity:{Rarity::Ultra,Rarity::Super}){const auto base=openBag(c,rarity,271,1,1),bonus=openBag(c,rarity,271,1,3);unsigned a=0,b=0;CHECK_EQ(base.visible.size(),bonus.visible.size());for(std::size_t i=0;i<base.visible.size();++i)CHECK_EQ(base.visible[i].index,bonus.visible[i].index);for(const auto&[id,n]:base.loot[0])a+=n;for(const auto&[id,n]:bonus.loot[0])b+=n;CHECK(b>a);}
}
TEST(garbage_guarantee_pays_each_type_when_all_normal_trials_miss){
    const auto dir=std::filesystem::temp_directory_path()/"florr-garbage-guarantee";std::filesystem::create_directories(dir);
    for(const auto* file:{"mobs.json","petals.json"})std::filesystem::copy_file(testsupport::dataFile(file),dir/file,std::filesystem::copy_options::overwrite_existing);
    std::ofstream(dir/"mob_drops.json")<<R"({"garbage":{"autoEggDrops":false,"drops":[{"type":"petal","itemType":"poo","rarity":"common","probability":1,"minQuantity":1,"maxQuantity":1,"rarityChances":[[[0,0.000001]]]}]}})";
    ContentRegistry c;std::string error;CHECK(c.load(dir.string(),error));const auto result=openBag(c,Rarity::Common,27);
    CHECK_EQ(result.loot[0].size(),result.visible.size());for(const auto&[id,n]:result.loot[0])CHECK_EQ(n,1u);
    std::filesystem::remove(dir/"mobs.json");std::filesystem::remove(dir/"petals.json");std::filesystem::remove(dir/"mob_drops.json");std::filesystem::remove(dir);
}
