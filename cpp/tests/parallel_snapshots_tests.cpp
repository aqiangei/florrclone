#include "test.h"
#include "server/parallel_snapshots.h"
#include <cstring>

using namespace flix;

TEST(parallel_snapshots_match_serial_bytes_across_frames_and_membership_changes) {
    World world;
    std::vector<Entity> players, mobs;
    std::uint32_t id = 0;
    auto entity = [&](Vec2 at, bool player) {
        const Entity e = world.create();
        world.add<Transform>(e, Transform{at, 0.0});
        world.add<Body>(e, Body{30, 1});
        world.add<Health>(e, Health{100, 100, 0, 0});
        world.add<NetId>(e, NetId{++id});
        Replicated rep;
        rep.kind = player ? net::EntityKind::Player : net::EntityKind::Mob;
        world.add<Replicated>(e, rep);
        if (player) {
            world.add<PlayerTag>(e);
            world.add<PlayerLocation>(e);
            world.add<PlayerAccount>(e, PlayerAccount{"account" + std::to_string(id), "viewer", id, false});
        }
        return e;
    };
    for (int i = 0; i < 8; ++i) players.push_back(entity({i * 300.0, 1000}, true));
    for (int i = 0; i < 120; ++i) mobs.push_back(entity({i * 55.0, 1000}, false));
    ParallelSnapshots parallel(3), serial(0);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    CHECK_EQ(parallel.workers(), 3u);
#endif
    std::vector<ClientView> parallelViews(8), serialViews(8);
    std::vector<ParallelSnapshots::Job> a, b;
    EventQueue events;
    for (unsigned frame = 0; frame < 20; ++frame) {
        const unsigned count = frame % 5 == 0 ? 1 : (frame % 7 == 0 ? 0 : 8);
        a.resize(count); b.resize(count);
        events.clear();
        events.damage(1, 5, {1000, 1000}, Realm::Overworld, 0, players[0], 1);
        if (frame == 6) world.destroy(mobs.back());
        if (frame == 9) world.get<Transform>(players[3]).realm = Realm::Arena;
        if (frame == 12) { parallelViews[2].reset(); serialViews[2].reset(); }
        world.get<Transform>(mobs.front()).position.x += 13;
        for (unsigned i = 0; i < count; ++i) {
            a[i].viewer = b[i].viewer = players[i];
            a[i].view = &parallelViews[i]; b[i].view = &serialViews[i];
            a[i].squad = b[i].squad = {players[(i + 1) % players.size()]};
            a[i].frame.tick = b[i].frame.tick = frame * 3;
            a[i].frame.snapshotIndex = b[i].frame.snapshotIndex = frame;
            a[i].frame.nowMillis = b[i].frame.nowMillis = 1000 + frame * 50;
            a[i].frame.events = b[i].frame.events = &events;
            if (i == 0) a[i].frame.viewport = b[i].frame.viewport = Vec2{2400, 1600};
        }
        serial.build(world, b);
        parallel.build(world, a);
        for (unsigned i = 0; i < count; ++i) {
            CHECK_EQ(a[i].output.size(), b[i].output.size());
            if (a[i].output.size() == b[i].output.size())
                CHECK(std::memcmp(a[i].output.data(), b[i].output.data(), a[i].output.size()) == 0);
            CHECK_EQ(parallelViews[i].tracked.size(), serialViews[i].tracked.size());
        }
    }
}
