// Isolated single-slot dummy benchmark using the shipping simulation systems.
// Usage: petal_dps_bench DATA_DIR [rarity|all] [petal|all] [seconds=60]
// CSV goes to stdout; progress goes to stderr. No accounts or saves are opened.
#include "server/systems/petals.h"
#include "server/systems/combat.h"
#include "server/systems/movement.h"
#include "server/systems/mob_ai.h"
#include "shared/game/terrain.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace flix;

struct Result { double damage = 0; unsigned hits = 0; };

Result measure(std::uint16_t index, Rarity rarity, double separation,
               std::uint8_t flags, double seconds) {
    World world;
    CommandBuffer commands{world};
    PetalSystem petals;
    CombatSystem combat;
    MovementSystem movement;
    MobAiSystem ai{world, 12345};
    Terrain terrain;
    SpatialGrid grid;
    EventQueue events;
    const Entity player = world.create();
    const Vec2 origin{10000, 10000};
    world.add<PlayerTag>(player);
    world.add<Transform>(player, Transform{origin, 0});
    world.add<Motion>(player);
    world.add<Knockback>(player);
    world.add<Body>(player, Body{kPlayerBaseRadius, 1});
    world.add<Health>(player, Health{125,125,0,1e100});
    world.add<Faction>(player, Faction{Team::Players,false});
    world.add<PlayerProgress>(player);
    world.add<PlayerInput>(player);
    world.add<PlayerModifiers>(player);
    world.add<PlayerLocation>(player);
    world.add<Loadout>(player);
    world.add<PetalRing>(player);
    world.add<Afflictions>(player);
    world.add<HitCooldowns>(player);
    world.get<Loadout>(player).slots[0] = LoadoutSlot{index,rarity,0,false};
    world.get<PlayerInput>(player).current.flags = flags;
    world.get<PlayerInput>(player).current.aimAngle = 0;
    const Entity dummy = world.create();
    const Vec2 target{origin.x + separation, origin.y};
    world.add<Transform>(dummy, Transform{target,0});
    world.add<Body>(dummy, Body{50,1e20});
    world.add<Health>(dummy, Health{1e30,1e30,0,0});
    world.add<Faction>(dummy, Faction{Team::Hostiles,false});
    world.add<MobTag>(dummy);
    world.add<NpcTag>(dummy);
    world.add<NetId>(dummy, NetId{1});
    world.add<Afflictions>(dummy);
    world.add<HitCooldowns>(dummy);
    const std::vector<RealmPoint> active{{origin,Realm::Overworld}};
    Query<Transform,Body> bodies{world};
    bodies.without<ProjectileTag,Intangible>();
    auto rebuild = [&] {
        grid.clear();
        bodies.each([&](Entity e, Transform& t, Body& b) {
            grid.insert(e,t.realm,t.position,b.radius);
        });
    };
    Result result;
    const int warmTicks = static_cast<int>(10 / net::kTickSeconds);
    const int ticks = static_cast<int>(seconds / net::kTickSeconds);
    for (int tick = 0; tick < warmTicks + ticks; ++tick) {
        const double now = 1000 + (tick+1)*net::kTickMillis;
        commands.flush();
        events.clear();
        // Fixed position and an invulnerable flower isolate outgoing damage.
        world.get<Transform>(player).position = origin;
        world.get<Motion>(player) = Motion{};
        world.get<Transform>(dummy).position = target;
        petals.foldModifiers(world,content());
        movement.runPlayerPhase(world,terrain,now,net::kTickSeconds);
        petals.run(world,content(),now,net::kTickSeconds,commands,&terrain,&events);
        rebuild();
        combat.beginTick(world,now,net::kTickSeconds,events);
        combat.runContactPhase(world,grid,content(),now);
        ai.run(world,terrain,grid,active,now,net::kTickSeconds,commands);
        movement.runWorldPhase(world,terrain,now,net::kTickSeconds);
        ai.placeChains(world,terrain,active);
        ai.tickPetalRings(world,commands);
        rebuild();
        combat.runWorldPhase(world,grid,content(),now,net::kTickSeconds);
        petals.landShots(world,content(),combat.shotLandings(),&terrain);
        if (tick >= warmTicks) for (const auto& event : events.events()) {
            if (event.kind == net::EventKind::Damage && event.struck == dummy &&
                event.dealtBy == player && !(event.flag & net::DamageFlashOnly)) {
                result.damage += event.amount;
                ++result.hits;
            }
        }
        if (!world.isAlive(player) || world.has<Dead>(player))
            throw std::runtime_error("benchmark player died");
    }
    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string error;
    if (!loadContent(argv[1],error)) { std::fprintf(stderr,"%s\n",error.c_str()); return 1; }
    const std::string tier = argc > 2 ? argv[2] : "all";
    const std::string filter = argc > 3 ? argv[3] : "all";
    const double seconds = argc > 4 ? std::atof(argv[4]) : 60;
    if (!(seconds > 0)) return 2;
    std::printf("petal,rarity,distance,mode,seconds,damage,hits,dps\n");
    for (std::uint16_t i=0; i<content().petalCount(); ++i) {
        const auto& config = content().petal(i);
        if (filter != "all" && filter != config.id) continue;
        for (int r=0; r<kRarityCount; ++r) {
            const Rarity rarity = static_cast<Rarity>(r);
            if (tier != "all" && tier != rarityName(rarity)) continue;
            if (r < rarityIndex(config.minRarity)) continue;
            for (double separation : {75.0,150.0,300.0}) {
                for (bool attack : {true,false}) {
                    const Result v=measure(i,rarity,separation,
                        attack ? net::InputAttack : net::InputDefend,seconds);
                    std::printf("%s,%s,%.0f,%s,%.6f,%.12g,%u,%.12g\n",
                        config.id.c_str(),rarityName(rarity),separation,
                        attack?"attack":"defend",seconds,v.damage,v.hits,v.damage/seconds);
                }
            }
        }
        std::fprintf(stderr,"done %s\n",config.id.c_str());
        std::fflush(stdout);
    }
}
