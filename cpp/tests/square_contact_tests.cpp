#include "test.h"
#include "test_data.h"
#include "shared/game/body_contact.h"
#include "server/systems/combat.h"
#include "server/systems/movement.h"
using namespace flix;

TEST(square_contacts_follow_edges_and_rotated_corners) {
    CHECK(circleSquareContact({0,0},10,0,{13,0},2).depth<0);
    CHECK(circleSquareContact({0,0},10,0,{11,0},2).depth>0);
    CHECK(circleSquareContact({0,0},10,0,{12,12},2).depth<0);
    CHECK(circleSquareContact({0,0},10,kPi*.25,{15,0},2).depth>0);
    CHECK(circleSquareContact({0,0},10,kPi*.25,{0,17},2).depth<0);
    CHECK(!segmentTouchesSquare({-20,13},{20,13},{0,0},10,0,2));
    CHECK(segmentTouchesSquare({-20,11},{20,11},{0,0},10,0,2));
    CHECK(segmentTouchesSquare({-20,0},{20,0},{0,0},10,kPi*.25,1));
    CHECK(shapedBodyContact({0,0},15,10,0,{21,0},15,10,0).depth<0);
    CHECK(shapedBodyContact({0,0},15,10,0,{19,0},15,10,0).depth>0);
}

TEST(square_melee_does_not_hit_the_empty_space_beside_its_face) {
    std::string error; CHECK(loadContent(testsupport::dataDir(),error));
    const auto& c=content();
    auto healthAfter=[&](double extra,double angle) {
        World w; CommandBuffer commands{w}; EventQueue events; SpatialGrid grid; CombatSystem combat;
        auto square=w.create();
        w.add<MobTag>(square); w.add<MobType>(square,MobType{c.mobIndex("square"),Rarity::Common,1});
        w.add<Transform>(square,Transform{{5000,5000},angle});
        w.add<Body>(square,Body{100,1}); w.add<Health>(square,Health{1000,1000});
        w.add<Faction>(square,Faction{Team::Hostiles,false});
        w.add<ContactDamage>(square,ContactDamage{10});
        const double half=squareMobHalfExtent(w,square);
        auto player=w.create();
        w.add<PlayerTag>(player);
        w.add<Transform>(player,Transform{Vec2{5000,5000}+contactRotate({half+10+extra,0},angle),0});
        w.add<Body>(player,Body{10,1}); w.add<Health>(player,Health{1000,1000});
        w.add<Faction>(player,Faction{Team::Players,false});
        Query<Transform,Body> q{w};
        q.each([&](Entity e,Transform& t,Body& b){grid.insert(e,t.realm,t.position,b.radius);});
        combat.run(w,grid,c,1000,.05,commands,events); commands.flush();
        return w.get<Health>(player).current;
    };
    for(double angle:{0.0,kPi*.25}) {
        CHECK_NEAR(healthAfter(2,angle),1000,1e-9);
        CHECK(healthAfter(-2,angle)<1000);
    }
}

TEST(square_mob_separation_uses_its_face_instead_of_the_corner_circle) {
    std::string error; CHECK(loadContent(testsupport::dataDir(),error));
    const auto& c=content();
    for(double extra:{2.0,-2.0}) {
        World w; Terrain terrain; MovementSystem movement;
        const auto spawn=[&](const char* id,Vec2 p,double radius) {
            auto e=w.create(); w.add<MobTag>(e);
            w.add<MobType>(e,MobType{c.mobIndex(id),Rarity::Common,1});
            w.add<Transform>(e,Transform{p,0}); w.add<Body>(e,Body{radius,1});
            w.add<Motion>(e); w.add<Health>(e,Health{1000,1000});
            w.add<Faction>(e,Faction{Team::Hostiles,false}); return e;
        };
        auto square=spawn("square",{5000,5000},100);
        const auto start=Vec2{5000+squareMobHalfExtent(w,square)+
            10*mobBodyContactScale("ladybug")+kMobCollisionBuffer+extra,5000};
        auto other=spawn("ladybug",start,10);
        movement.run(w,terrain,1000,0);
        if(extra>0) CHECK_NEAR(w.get<Transform>(other).position.x,start.x,1e-9);
        else CHECK(w.get<Transform>(other).position.x>start.x);
    }
}
