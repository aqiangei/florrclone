#include "test.h"
#include "test_data.h"
#include "server/systems/petals.h"
#include "server/systems/movement.h"
using namespace flix;

TEST(powder_rarities_and_equipped_slots_have_distinct_additive_speed) {
    const auto& c=testsupport::shippedContent();
    const auto id=c.petalIndex("mysterious_powder");
    double previous=1;
    for(int i=0;i<kLadderRarityCount;++i) {
        const auto speed=c.petalStats(id,static_cast<Rarity>(i)).modifiers.speed;
        CHECK(speed>previous); previous=speed;
    }
    World w; PetalSystem petals; CommandBuffer commands{w};
    auto p=w.create();
    w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{5000,5000},0});
    w.add<Health>(p,Health{100,100}); w.add<Body>(p,Body{25,1});
    w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
    w.add<PlayerInput>(p);
    for(int i=0;i<2;++i) w.get<Loadout>(p).slots[i]={id,Rarity::Apex};
    petals.run(w,c,0,.05,commands); commands.flush();
    CHECK_NEAR(w.get<PlayerModifiers>(p).speedScale,2.8,1e-9);
}

TEST(powder_running_momentum_survives_a_bubble_pop) {
    const auto& c=testsupport::shippedContent();
    auto pop=[&](bool powder) {
        World w; PetalSystem petals; CommandBuffer commands{w};
        auto p=w.create();
        w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{5000,5000},0});
        w.add<Motion>(p); w.add<Health>(p,Health{100,100});
        w.add<Body>(p,Body{25,1}); w.add<PlayerModifiers>(p);
        w.add<PetalRing>(p); w.add<Loadout>(p); w.add<PlayerInput>(p);
        w.get<Loadout>(p).slots[0]={c.petalIndex("bubble"),Rarity::Common};
        w.get<Loadout>(p).slots[1]={c.petalIndex(powder?"mysterious_powder":"basic"),Rarity::Apex};
        for(int i=0;i<200;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
        // Run away from the bubble, so its impulse and running speed align.
        Vec2 bearing{1,0};
        Query<PetalInstance,Transform> q{w};
        q.each([&](Entity,PetalInstance& petal,Transform& at){
            if(petal.slot==0) bearing=(Vec2{5000,5000}-at.position).normalized();
        });
        const double run=kPlayerMaxSpeed*w.get<PlayerModifiers>(p).speedScale;
        w.get<Motion>(p).velocity=bearing*run;
        w.get<PlayerInput>(p).current.flags=net::InputDefend;
        petals.run(w,c,10000,.05,commands);commands.flush();
        CHECK(w.get<Motion>(p).velocity.length()>run);
        return w.get<Motion>(p).velocity;
    };
    const auto plain=pop(false),boosted=pop(true);
    CHECK(boosted.length()>plain.length());
}

TEST(uncapped_high_speed_checks_walls_and_retains_open_ground_travel) {
    Terrain terrain;
    Vec2 p{3000,5000};
    auto free=stepCollide(terrain,Realm::Overworld,p,{30000,0},25,.05);
    CHECK(!free.blocked); CHECK_NEAR(free.displacement.x,1500,1e-6);
    for(int y=0;y<terrain.tileRows();++y) terrain.setTile(10,y,Tile::Solid);
    p={10*kTileSize-100,5000};
    const auto hit=stepCollide(terrain,Realm::Overworld,p,{30000,0},25,.05);
    CHECK(hit.blocked); CHECK(p.x<=10*kTileSize-25+1e-6);
}

TEST(a_full_bubble_ring_keeps_impulse_above_the_previous_speed_ceiling) {
    const auto& c=testsupport::shippedContent();
    World w; PetalSystem petals; CommandBuffer commands{w};
    auto p=w.create();
    w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{20000,20000},0});
    w.add<Motion>(p); w.add<Health>(p,Health{100,100}); w.add<Body>(p,Body{25,1});
    w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p); w.add<PlayerInput>(p);
    for(int i=0;i<kLoadoutActiveSlots;++i)
        w.get<Loadout>(p).slots[i]={c.petalIndex("bubble"),Rarity::Apex};
    for(int i=0;i<200;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
    w.get<PlayerInput>(p).current.flags=net::InputDefend;
    petals.run(w,c,10000,.05,commands);commands.flush();
    const auto velocity=w.get<Motion>(p).velocity;
    CHECK(velocity.length()>kMinSubstepLength*kMaxSubstepCount/.05);
    Terrain terrain; auto at=w.get<Transform>(p).position;
    const auto step=stepCollide(terrain,Realm::Overworld,at,velocity,25,.05);
    CHECK(!step.blocked);
    CHECK_NEAR(step.displacement.length(),velocity.length()*.05,1e-5);
}
