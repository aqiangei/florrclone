#include "test.h"
#include "test_data.h"
#include "server/systems/flower_mobs.h"
#include "server/systems/combat.h"
#include "shared/net/protocol.h"
#include <set>

using namespace flix;
namespace {
const ContentRegistry& data(){static ContentRegistry c;static bool ready=false;if(!ready){std::string e;CHECK(c.loadFiles(testsupport::dataFile("mobs.json"),testsupport::dataFile("petals.json"),e));ready=true;}return c;}
Entity mob(World& w,const char* id,Rarity rarity=Rarity::Common){
    const auto index=data().mobIndex(id);const auto stats=data().mobStats(index,rarity);
    const auto e=w.create();w.add<MobTag>(e);w.add<MobType>(e,MobType{index,rarity,1});
    w.add<Transform>(e,Transform{{1000,1000},0});w.add<Body>(e,Body{stats.radius,1});
    w.add<Health>(e,Health{stats.health,stats.health,0,0});w.add<MobAi>(e);
    w.add<Faction>(e,Faction{Team::Hostiles,false});w.add<NetId>(e,NetId{1});return e;
}
}
TEST(flower_mobs_have_real_breakable_replicated_petals_at_each_rarity){
    for(const auto id:{"gambler","mecha_flower"})for(int tier=0;tier<=8;++tier){
        World w;const auto owner=mob(w,id,clampRarity(tier));std::uint32_t next=100;
        stepFlowerMobs(w,data(),1000,net::kTickSeconds,[&]{return next++;});
        const auto ring=w.get<FlowerMobRing>(owner);int count=0;std::set<std::string> types;
        for(auto e:ring.petals)if(e!=NULL_ENTITY){
            ++count;CHECK(w.isAlive(e));CHECK(w.has<Health>(e));CHECK(w.has<ContactDamage>(e));
            CHECK(w.has<NetId>(e));CHECK_EQ(w.get<MobRingPetal>(e).mob,owner);
            const auto visual=w.get<Replicated>(e);CHECK(visual.kind==net::EntityKind::Petal);
            const auto stats=data().petalStats(visual.typeIndex,clampRarity(tier));
            CHECK_NEAR(w.get<ContactDamage>(e).amount,stats.damage,1e-8);
            CHECK_NEAR(w.get<Health>(e).max,stats.health,1e-8);
            CHECK_NEAR(w.get<Body>(e).radius,9,1e-8);
            types.insert(data().petal(visual.typeIndex).id);
        }
        CHECK_EQ(count,std::min(10,5+tier));
        CHECK_EQ(types.size(),std::string(id)=="gambler"?5u:1u);
        CHECK(data().mob(data().mobIndex(id)).image.find("clipPath")==std::string::npos);
    }
}
TEST(flower_mob_petals_rotate_break_reload_and_leave_no_orphan_hitboxes){
    World w;const auto owner=mob(w,"gambler");
    stepFlowerMobs(w,data(),1000,.1,{});const auto first=w.get<FlowerMobRing>(owner).petals[0];
    const auto start=w.get<Transform>(first).position;
    stepFlowerMobs(w,data(),1100,.1,{});
    CHECK((w.get<Transform>(first).position-start).length()>1);
    CombatSystem combat;
    const auto hit=combat.applyDamage(w,first,NULL_ENTITY,1e9,1100);
    CHECK(hit.killed);stepFlowerMobs(w,data(),1100,.01,{});
    CHECK_EQ(w.get<FlowerMobRing>(owner).petals[0],NULL_ENTITY);
    stepFlowerMobs(w,data(),3599,.1,{});CHECK_EQ(w.get<FlowerMobRing>(owner).petals[0],NULL_ENTITY);
    stepFlowerMobs(w,data(),3600,.001,{});CHECK(w.get<FlowerMobRing>(owner).petals[0]!=NULL_ENTITY);
    w.destroy(owner);stepFlowerMobs(w,data(),3700,.1,{});
    Query<FlowerMobPetal> petals{w};int remaining=0;petals.each([&](Entity,FlowerMobPetal&){++remaining;});
    CHECK_EQ(remaining,0);
}
TEST(flower_mob_orbits_use_reference_distance_and_mechanical_step_rotation){
    World w;const auto owner=mob(w,"mecha_flower");stepFlowerMobs(w,data(),1000,.1,{});
    const auto petal=w.get<FlowerMobRing>(owner).petals[0];const auto start=w.get<Transform>(petal).position;
    stepFlowerMobs(w,data(),1100,.1,{});CHECK_NEAR((w.get<Transform>(petal).position-start).length(),0,1e-9);
    stepFlowerMobs(w,data(),1700,.6,{});CHECK((w.get<Transform>(petal).position-start).length()>1);
    const double body=w.get<Body>(owner).radius;
    CHECK_NEAR((w.get<Transform>(petal).position-Vec2{1000,1000}).length(),body+55,1e-8);
    const auto target=w.create();w.add<Transform>(target,Transform{{1100,1000},0});
    w.add<Body>(target,Body{25,1});w.get<MobAi>(owner).target=target;
    stepFlowerMobs(w,data(),1800,.1,{});
    CHECK_NEAR((w.get<Transform>(petal).position-Vec2{1000,1000}).length(),body+95,1e-8);
    const auto movement=flowerMobPursuit(w,owner,{body+95+10,0},body,25,100,false);
    CHECK_NEAR(movement.length(),100,1e-8);CHECK(std::abs(movement.y)>99);
}
TEST(flower_mob_petals_damage_at_their_visible_circles_without_a_square_hull){
    for(const auto id:{"gambler","mecha_flower"}) {
        World w;CommandBuffer commands{w};EventQueue events;SpatialGrid grid;CombatSystem combat;
        const auto owner=mob(w,id);stepFlowerMobs(w,data(),1000,.1,{});
        const auto petal=w.get<FlowerMobRing>(owner).petals[0];
        const Vec2 at=w.get<Transform>(petal).position;
        const Vec2 direction=(at-Vec2{1000,1000})/(at-Vec2{1000,1000}).length();
        const auto player=w.create();w.add<PlayerTag>(player);
        w.add<Transform>(player,Transform{at,0});w.add<Body>(player,Body{5,1});
        w.add<Health>(player,Health{1e6,1e6});w.add<Faction>(player,Faction{Team::Players,false});
        auto rebuild=[&]{grid.clear();Query<Transform,Body> bodies{w};
            bodies.each([&](Entity e,Transform& t,Body& b){grid.insert(e,t.realm,t.position,b.radius);});};
        rebuild();combat.run(w,grid,data(),1000,net::kTickSeconds,commands,events);commands.flush();
        CHECK_NEAR(w.get<Health>(player).current,1e6-w.get<ContactDamage>(petal).amount,1e-8);
        const double health=w.get<Health>(player).current;
        w.get<Transform>(player).position=at+direction*30;
        rebuild();combat.run(w,grid,data(),2000,net::kTickSeconds,commands,events);commands.flush();
        CHECK_NEAR(w.get<Health>(player).current,health,1e-8);
    }
}
TEST(flower_mob_rings_stay_bounded_with_many_targets_and_repeated_ticks){
    World w;std::vector<Entity> owners;std::uint32_t next=100;
    for(int i=0;i<64;++i){
        const auto e=mob(w,i%2?"gambler":"mecha_flower",Rarity::Super);owners.push_back(e);
        const auto p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{1150,1000},0});
        w.add<Body>(p,Body{25,1});w.get<MobAi>(e).target=p;
    }
    for(int tick=0;tick<600;++tick)stepFlowerMobs(w,data(),1000+tick*net::kTickMillis,
                                                net::kTickSeconds,[&]{return next++;});
    CHECK_EQ(next,740u); // 64 mobs * 10 petals, with no duplicate respawns.
    Query<FlowerMobPetal,Transform> petals{w};int count=0;
    petals.each([&](Entity,FlowerMobPetal&,Transform& at){++count;CHECK(std::isfinite(at.position.x));CHECK(std::isfinite(at.position.y));});
    CHECK_EQ(count,640);
    for(const auto e:owners)w.destroy(e);
    stepFlowerMobs(w,data(),22000,net::kTickSeconds,{});
    count=0;petals.each([&](Entity,FlowerMobPetal&,Transform&){++count;});CHECK_EQ(count,0);
}
