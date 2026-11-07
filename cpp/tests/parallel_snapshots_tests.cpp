#include "test.h"
#include "server/parallel_snapshots.h"
#include "server/systems/loot.h"
#include "server/systems/movement.h"
#include <chrono>
#include <cstring>

using namespace flix;

TEST(parallel_snapshots_match_serial_bytes_across_frames_and_membership_changes) {
    World world;
    std::vector<Entity> players, mobs;
    NetIdAllocator ids;
    auto entity = [&](Vec2 at, bool player) {
        const Entity e = world.create();
        world.add<Transform>(e, Transform{at, 0.0});
        world.add<Body>(e, Body{30, 1});
        world.add<Health>(e, Health{100, 100, 0, 0});
        const std::uint32_t id = ids.next();
        world.add<NetId>(e, NetId{id});
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
    for (int i = 0; i < 32; ++i) players.push_back(entity({i * 300.0, 1000}, true));
    for (int i = 0; i < 120; ++i) mobs.push_back(entity({i * 55.0, 1000}, false));
    LootSystem loot;
    loot.netIds = &ids;
    const Entity stack = loot.spawnDrop(world, 2, Rarity::Super, {640, 1000},
                                        Realm::Overworld, {players[2], players[3]});
    loot.spawnDrop(world, 2, Rarity::Rare, {640, 1000}, Realm::Arena, {});
    loot.spawnDrop(world, 2, Rarity::Rare, {50000, 50000}, Realm::Overworld, {});
    SpatialGrid dropGrid;
    ServerWorkers workers(15);
    ParallelSnapshots parallel(workers, 15), serial(workers, 0);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    CHECK_EQ(workers.count(), 15u);
#endif
    std::vector<ClientView> parallelViews(32), serialViews(32);
    std::vector<ParallelSnapshots::Job> a, b;
    EventQueue events;
    for (unsigned frame = 0; frame < 20; ++frame) {
        const unsigned count = frame % 5 == 0 ? 1 : (frame % 7 == 0 ? 0 : 32);
        a.resize(count); b.resize(count);
        events.clear();
        events.damage(1, 5, {1000, 1000}, Realm::Overworld, 0, players[0], 1);
        if (frame == 6) world.destroy(mobs.back());
        if (frame == 9) world.get<Transform>(players[3]).realm = Realm::Arena;
        if (frame == 12) { parallelViews[2].reset(); serialViews[2].reset(); }
        world.get<Transform>(mobs.front()).position.x += 13;
        if (frame == 4) world.get<DropItem>(stack).quantity += 3;
        if (frame == 8) world.get<Transform>(stack).position.x = 1300;
        if (frame == 11) world.destroy(stack);
        dropGrid.clear();
        Query<DropTag, Transform>{world}.each([&](Entity e, DropTag&, Transform& at) {
            dropGrid.insert(e, at.realm, at.position);
        });
        for (unsigned i = 0; i < count; ++i) {
            a[i].viewer = b[i].viewer = players[i];
            a[i].view = &parallelViews[i]; b[i].view = &serialViews[i];
            a[i].squad = b[i].squad = {players[(i + 1) % players.size()]};
            a[i].frame.tick = b[i].frame.tick = frame * 3;
            a[i].frame.snapshotIndex = b[i].frame.snapshotIndex = frame;
            a[i].frame.nowMillis = b[i].frame.nowMillis = 1000 + frame * 50;
            a[i].frame.events = b[i].frame.events = &events;
            a[i].frame.dropGrid = count > 1 ? &dropGrid : nullptr;
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

TEST(parallel_simulation_matches_serial_with_dense_mobs_and_drop_stacks) {
    struct Scene {
        World world;
        Terrain terrain;
        SpatialGrid grid;
        ContentRegistry content;
        CommandBuffer commands{world};
        EventQueue events;
        MovementSystem movement;
        LootSystem loot;
        Rng rng{19};
        std::vector<Entity> mobs, drops;
        double elapsedMillis = 0;

        Scene(ServerWorkers* workers) {
            movement.workers = loot.workers = workers;
            movement.workerLimit = loot.workerLimit = 15;
            loot.terrain = &terrain;
            const Entity player = world.create();
            world.add<PlayerTag>(player);
            world.add<Transform>(player, Transform{{200, 200}});
            world.add<PlayerModifiers>(player);
            for (int i = 0; i < 1024; ++i) {
                const Entity mob = world.create();
                mobs.push_back(mob);
                world.add<MobTag>(mob);
                world.add<MobType>(mob);
                world.add<Transform>(mob, Transform{{1000.0 + (i % 32) * 12, 1000.0 + (i / 32) * 12}});
                world.add<Motion>(mob, Motion{{double(i % 7), -3}});
                world.add<Body>(mob, Body{19, 1});
                world.add<Knockback>(mob, Knockback{});
            }
            for (int i = 0; i < 8192; ++i) {
                const Vec2 at = i == 1 ? Vec2{200, 200} : Vec2{30.0 + (i % 128) * 25, 500.0 + (i / 128) * 25};
                const Entity drop = loot.spawnDrop(world, 0, Rarity::Common, at, Realm::Overworld, {player});
                drops.push_back(drop);
                world.get<DropItem>(drop).quantity = 30;
                if (i % 16 == 0) world.get<Lifetime>(drop).remainingSeconds = .07;
            }
        }

        void tick(double time) {
            grid.clear();
            Query<DropTag, Transform, Body>{world}.each([&](Entity e, DropTag&, Transform& at, Body& body) {
                grid.insert(e, at.realm, at.position, body.radius);
            });
            const auto start = std::chrono::steady_clock::now();
            movement.runWorldPhase(world, terrain, time, .05);
            loot.run(world, grid, content, rng, time, .05, commands, events);
            elapsedMillis += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            commands.flush();
        }
    };

    ServerWorkers workers(15);
    Scene serial(nullptr), parallel(&workers);
    for (int i = 0; i < 12; ++i) {
        serial.tick(i * 50);
        parallel.tick(i * 50);
        CHECK_EQ(parallel.loot.pickups().size(), serial.loot.pickups().size());
        if (i == 0) {
            CHECK_EQ(parallel.loot.pickups().size(), 1u);
            if (!parallel.loot.pickups().empty()) CHECK_EQ(parallel.loot.pickups()[0].quantity, 30u);
        }
    }
    CHECK(!parallel.world.isAlive(parallel.drops[0]));
    CHECK(parallel.world.isAlive(parallel.drops[2]));
    for (Entity mob : serial.mobs) {
        const Vec2 a = serial.world.get<Transform>(mob).position;
        const Vec2 b = parallel.world.get<Transform>(mob).position;
        CHECK(a.x == b.x && a.y == b.y);
    }
    for (Entity drop : serial.drops) {
        CHECK_EQ(parallel.world.isAlive(drop), serial.world.isAlive(drop));
        if (!serial.world.isAlive(drop) || !parallel.world.isAlive(drop)) continue;
        CHECK_EQ(parallel.world.get<Lifetime>(drop).remainingSeconds,
                 serial.world.get<Lifetime>(drop).remainingSeconds);
    }
    std::printf("[dense phases] serial %.2fms/tick, %u workers %.2fms/tick\n",
                serial.elapsedMillis / 12, workers.count(), parallel.elapsedMillis / 12);
}
