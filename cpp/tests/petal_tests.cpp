#include "test.h"
#include "shared/game/berry_projectile.h"

#include "server/replication.h"
#include "server/hyper_petal_loadout.h"
#include "shared/game/npc.h"
#include "test_data.h"
#include "server/systems/combat.h"
#include "server/systems/movement.h"
#include "server/systems/npcs.h"
#include "server/systems/petals.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "fixture_content.h"

using namespace flix;

namespace {

// The ring is driven entirely by content, so these tests run on content of
// their own: a handful of petals chosen to isolate one rule each. Using the
// shipped data instead would tie every assertion to a balance number that is
// allowed to change.

const char* const kPetalsJson = R"JSON({
  "lentil":{"name":"Lentil","damage":13,"health":12,"size":1,"cooldown":2000,"count":1,"attractionForce":2000,"playerModifiers":{"petalAttractionRadius":20}},
  "dice":{"name":"Dice","damage":10,"health":10,"size":2,"cooldown":1200,"count":1},
  "basic":    {"name":"Basic","damage":10,"health":10,"size":2,"cooldown":1200,"count":1,"color":"#90EE90"},
  "bomb":     {"name":"Bomb","damage":10,"health":5,"size":1,"cooldown":2000,"count":1},
  "icosahedron":{"name":"Icosahedron","damage":10,"health":10,"size":2,"cooldown":2500,"count":1,"color":"#8B5CF6"},
  "sandy":    {"name":"Sandy","damage":4,"health":12,"size":1,"cooldown":800,"count":4,"clumped":true,"color":"#8B0000"},
  "spready":  {"name":"Spready","damage":4,"health":12,"size":1,"cooldown":800,"count":3,"clumped":true,"clumpSpacing":2.5,"color":"#F0BD48"},
  "hanging":  {"name":"Hanging","damage":4,"health":12,"size":1,"cooldown":800,"count":3,"clumped":true,"clumpSpacing":2.5,"clumpOutsideRing":true,"color":"#F0BD48"},
  "shards":   {"name":"Shards","damage":3,"health":6,"size":1,"cooldown":500,"count":3,"independentHealth":true,"color":"#CCCCCC"},
  "rock":     {"name":"Rock","damage":1,"size":2,"cooldown":1000,"count":1,"color":"#777777"},
  "air":{"name":"Air","damage":0,"health":1,"size":1,"cooldown":1000,"count":0,"invulnerable":true,"playerModifiers":{"playerRadius":1.1}},
  "rose":{"name":"Rose","damage":1,"health":5,"size":1,"cooldown":1000,"burstHeal":30},
  "dahlia":{"name":"Dahlia","damage":1,"health":5,"size":1,"cooldown":1000,"burstHeal":20},
  "mana_orb":{"name":"Mana Orb","damage":1,"health":5,"size":1,"cooldown":1000,"baseMaxMana":20,"burstMana":10},
  "yggdrasil":{"name":"Yggdrasil","damage":1,"health":1,"size":1,"cooldown":1000},
  "honey":{"name":"Honey","damage":1,"health":5,"size":1,"cooldown":1000},
  "missile":{"name":"Missile","damage":5,"health":5,"size":1,"cooldown":1000,"projectile":{"count":1,"speed":300,"distance":500}},
  "magic_missile":{"name":"Magic Missile","damage":5,"health":5,"size":1,"cooldown":1000,"projectile":{"count":1,"speed":300,"distance":500}},
  "grapes":{"name":"Grapes","damage":5,"health":5,"size":1,"cooldown":1000,"defendOnly":true,"projectile":{"count":1,"speed":640,"distance":1000}},
  "healer":   {"name":"Healer","damage":1,"health":5,"size":1,"cooldown":3500,"count":1,"burstHeal":10,"burstHealChargeMs":1000,"defendOnly":true,"color":"#FF69B4"},
  "shell":    {"name":"Shell","damage":1,"health":5,"size":1,"cooldown":3500,"count":1,"burstShield":22,"burstHealChargeMs":1000,"defendOnly":true,"color":"#FCDD86"},
  "bubble":   {"name":"Bubble","damage":0,"health":1,"size":1,"cooldown":1000,"count":1,"color":"#FFFFFF"},
  "web":      {"name":"Web","damage":5,"health":10,"size":1.2,"cooldown":3000,"count":1,"defendOnly":true,"webRadius":90,"color":"#FFFFFF"},
  "electric_web":{"name":"Electric Web","damage":5,"health":10,"size":1,"cooldown":3000,"count":1,"defendOnly":true,"webRadius":90,"color":"#44E9D5"},
  "pollen":   {"name":"Pollen","damage":10,"health":10,"size":1,"cooldown":1000,"count":1,"color":"#FFE763"},
  "uranium":  {"name":"Uranium","damage":4,"health":10,"size":1,"cooldown":2500,"count":1,"radiation":{"radius":160,"intervalMs":1000,"poisonDurationMs":2000,"selfPoisonScale":1},"color":"#63bf2e"},
  "sponge":   {"name":"Sponge","damage":10,"health":10,"size":1,"cooldown":2000,"count":1,"spongeDamageDuration":1000,"color":"#FF96E0"},
  "root":     {"name":"Root","damage":10,"health":10,"size":1,"cooldown":1000,"count":1,"armorPerStack":12,"defendOnly":true,"color":"#B86C32"},
  "peas":     {"name":"Peas","damage":6,"health":5,"size":1,"cooldown":1000,"count":1,"defendOnly":true,"projectile":{"count":3,"spreadAngle":0.5,"speed":800,"distance":1000},"color":"#00FF00"},
  "magic_peas":{"name":"Blueberries","damage":0,"health":5,"size":1,"cooldown":1000,"count":1,"defendOnly":true,"lightningDamage":true,"projectile":{"count":1,"speed":800,"distance":1000}},
  "mecha_missile":{"name":"Mecha Missile","damage":35,"health":2,"size":1,"cooldown":1500,"count":1,"projectile":{"count":1,"spreadAngle":0,"speed":600,"distance":2400}},
  "antennae":{"name":"Antennae","damage":0,"health":1,"count":0,"noPhysics":true,"equipFlags":"Antennae"},
  "shovel":{"name":"Shovel","damage":0,"health":null,"count":1,"cooldown":3000,"burrowChargeMillis":2000,"burrowDurationMillis":2000},
  "raindrop":{"name":"Raindrop","damage":20,"health":30,"count":1,"cooldown":1000},
  "pod":      {"name":"Pod","damage":2.5,"health":10,"size":1,"cooldown":1000,"count":4,"clumped":true,"defendOnly":true,"projectile":{"count":1,"spreadAngle":1.5708,"speed":800,"distance":1000},"color":"#8AC255"},
  "peaclump": {"name":"Peaclump","damage":6,"health":5,"size":1,"cooldown":1000,"count":4,"clumped":true,"projectile":{"count":1,"spreadAngle":0,"speed":800,"distance":1000},"color":"#00FF00"},
  "emitter":  {"name":"Emitter","damage":2,"health":null,"size":1,"cooldown":1000,"count":1,"projectile":{"count":2,"spreadAngle":0.3,"speed":400,"distance":400},"color":"#00FF00"},
  "lucky":    {"name":"Lucky","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"luck":2,"speed":1.5,"magnetism":50},"color":"#FFD700"},
  "reacher":  {"name":"Reacher","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"attackRange":50},"color":"#00FFFF"},
  "anchor":   {"name":"Anchor","damage":1,"health":5,"size":1,"cooldown":1000,"count":1,"playerModifiers":{"rotationSpeed":0},"color":"#888888"},
  "inflator": {"name":"Inflator","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"playerRadius":1.5},"color":"#FF00FF"},
  "stinky":   {"name":"Stinky","damage":0,"health":1,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"aggroRange":0.75},"color":"#8B4513"},
  "glowy":    {"name":"Glowy","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"aggroRadius":150},"color":"#FFFF00"},
  "bright":   {"name":"Bright","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"aggroRange":1.33},"color":"#FFFF00"},
  "charm":    {"name":"Charm","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"evasion":0.1},"color":"#FFF824"},
  "briny":    {"name":"Briny","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"damageReflection":0.25},"color":"#FFFFFF"},
  "brinier":  {"name":"Brinier","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"playerModifiers":{"damageReflection":0.4},"color":"#FFFFFF"},
  "summoner": {"name":"Summoner","damage":1,"health":4,"size":1,"cooldown":1000,"count":1,"petMobType":"critter","petMobRarity":"common","petCount":2,"color":"#AA00AA"},
  "stormcaller":{"name":"Stormcaller","damage":1,"health":10,"size":1,"cooldown":1000,"count":1,"petMobType":"critter","petCount":1,"petLifetimeMs":20000,"petManaCost":20,"color":"#42E3F5"},
  "toxic":    {"name":"Toxic","damage":2,"health":5,"size":1,"cooldown":1000,"count":1,"poison":0.05,"poisonDuration":3000,"color":"#00AA00"},
  "blade":    {"name":"Blade","damage":0,"health":null,"size":4,"cooldown":1,"count":0,"range":0,"bodyDamage":10,"equipFlags":"Cutter","noPhysics":true,"color":"#111111"},
  "sparkblade":{"name":"Spark Blade","damage":1,"health":null,"size":4,"cooldown":1,"count":0,"range":0,"bodyDamage":10,"equipFlags":"Cutter","noPhysics":true,"color":"#00FFFF"},
  "gazer":    {"name":"Gazer","damage":0,"health":null,"size":1,"cooldown":1,"count":0,"range":0,"fixedDirection":0,"equipFlags":"ThirdEye","noPhysics":true,"color":"#000000"},
  "lightning":{"name":"Lightning","damage":25,"health":10,"size":1,"cooldown":2500,"count":1,"color":"#FFFFFF"},
  "battery":  {"name":"Battery","damage":0,"health":null,"size":1,"cooldown":2500,"count":1,"color":"#FCDD86"},
  "capacitor":{"name":"Capacitor","damage":0,"health":null,"size":1.25,"cooldown":2500,"count":1,"color":"#000000"},
  "wing":     {"name":"Wing","damage":15,"health":10,"size":1,"cooldown":2500,"count":1,"color":"#FFFFFF"},
  "pearl":    {"name":"Pearl","damage":20,"health":50,"size":1.25,"cooldown":4000,"count":1,"color":"#FFFFFF"},
  "wax":      {"name":"Wax","damage":0,"health":1000,"size":1,"cooldown":30000,"count":1,"color":"#FFFF00"},
  "moon":     {"name":"Moon","damage":1,"health":1000,"size":2.6,"cooldown":10000,"count":1,"color":"#878787"},
  "cotton":   {"name":"Cotton","damage":0,"health":2,"size":1,"cooldown":1500,"count":1,"defendOnly":true,"color":"#FFFFFF"},
  "bone":     {"name":"Bone","damage":14,"health":10,"size":1,"cooldown":1500,"count":1,"petalArmor":10,"color":"#FFFFFF"},
  "vessel":   {"name":"Vessel","damage":1,"health":5,"size":1,"cooldown":1000,"count":1,"baseMaxMana":100,"color":"#42E3F5"},
  "orb":      {"name":"Orb","damage":1,"health":5,"size":1,"cooldown":3500,"count":1,"burstMana":10,"burstManaChargeMs":1000,"color":"#42E3F5"},
  "magicleaf":{"name":"Magic Leaf","damage":1,"health":5,"size":1,"cooldown":1000,"count":1,"passiveMana":5,"color":"#42E3F5"},
  "leafy":    {"name":"Leafy","damage":1,"health":5,"size":1,"cooldown":1000,"count":1,"passiveHeal":1,"color":"#39B54A"},
  "yuccaish": {"name":"Yuccaish","damage":1,"health":5,"size":1,"cooldown":1000,"count":1,"passiveHeal":1,"passiveHealDefendOnly":true,"color":"#74B53F"},
  "magicmissile":{"name":"Magic Missile","damage":6,"health":5,"size":1,"cooldown":1000,"count":1,"requiredMana":30,"projectile":{"count":1,"spreadAngle":0,"speed":800,"distance":1000},"color":"#42E3F5"},
  "magic_bubble":{"name":"Magic Bubble","damage":0,"health":1,"size":1,"cooldown":1000,"count":1,"requiredMana":40,"color":"#42E3F5"},
  "mimic":    {"name":"Mimic","damage":0,"health":0,"size":1,"cooldown":10000,"count":1,"color":"#FFA500"},
  "berries":  {"name":"Berries","damage":8,"health":10,"size":1,"cooldown":50,"count":4,"clumped":true,"defendOnly":true,"requiredMana":10,"lightningDamage":true,"projectile":{"count":1,"spreadAngle":1.5708,"speed":800,"distance":1000},"color":"#42E3F5"},
  "compass":  {"name":"Compass","damage":1,"health":40,"size":1.4,"cooldown":2500,"count":1,"color":"#3498DB"},
  "magnet":   {"name":"Magnet","damage":1,"health":5,"size":1,"cooldown":2000,"count":1,"color":"#000000"},
  "relic":    {"name":"Relic","damage":0,"health":null,"size":1,"cooldown":1,"count":0,"range":0,"fixedDirection":0,"noPhysics":true,"playerModifiers":{"maxHealth":1.1},"color":"#C7A138"},
  "champion_crown":{"name":"Champion's Crown","damage":0,"health":null,"size":1,"cooldown":1,"count":0,"range":0,"equipFlags":"Crown","noPhysics":true,"color":"#FFDC06"},
  "rubber":   {"name":"Rubber","damage":1,"health":30,"size":1.2,"cooldown":2500,"count":1,"color":"#EEEEEE"},
  "amulet":   {"name":"Amulet","damage":0,"health":10,"size":1,"cooldown":2000,"count":1,"defendOnly":true,"color":"#FF94C9"},
  "basil":    {"name":"Basil","damage":5,"health":15,"size":1.3,"cooldown":2500,"count":1,"defendOnly":true,"ignoresMobs":true,"color":"#319E40"},
  "disc":     {"name":"Disc","damage":0,"health":null,"size":1,"cooldown":1,"count":0,"range":0,"equipFlags":"Disc","noPhysics":true,"color":"#111111"},
  "bandage":  {"name":"Bandage","damage":0,"health":null,"size":1,"cooldown":1,"count":0,"range":0,"noPhysics":true,"color":"#E5DAA1"},
  "egg":{"name":"Ant Egg","damage":0,"health":1,"cooldown":60000,"count":4,"petMobType":"critter","petCount":4},
  "beetle_egg":{"name":"Beetle Egg","damage":0,"health":1,"cooldown":30000,"count":1,"petMobType":"critter","petCount":1},
  "golden_leaf":{"name":"Golden Leaf","damage":1,"health":100,"size":1.2,"cooldown":200,"count":1,"color":"#EBEB34"},
  "cursor":   {"name":"Cursor","damage":0,"health":null,"size":1,"cooldown":1,"count":1,"color":"#EEEEEE"},
  "ram":      {"name":"RAM","damage":10,"health":2,"size":1,"cooldown":3000,"count":1,"color":"#3498DB"},
  "sawblade": {"name":"Sawblade","damage":20,"health":10,"size":1,"cooldown":2500,"count":1,"defendOnly":true,"color":"#AAAAAA"},
  "carrot":   {"name":"Carrot","damage":18,"health":18,"size":1.4,"cooldown":1000,"count":1,"projectile":{"count":1,"distance":500,"speed":300,"spreadAngle":0},"color":"#F09148"},
  "saucer":   {"name":"Saucer","damage":15,"health":5,"size":1,"cooldown":1000,"count":1,"projectile":{"count":1,"distance":700,"speed":350,"spreadAngle":0,"summonMobType":"critter","summonMax":3},"color":"#7C7C7C"}
})JSON";

const char* const kMobsJson = R"JSON({
  "critter": {"name":"Critter","health":10,"damage":1,"size":1,"speed":0.2,"range":300,"cooldown":500,"color":"#FF0000","section":[0],"ai_type":"hostile"},
  "brute":   {"name":"Brute","health":500,"damage":5,"size":6,"speed":0.2,"range":300,"cooldown":500,"color":"#AA3300","section":[0],"ai_type":"hostile"},
  "puffball": {"name":"Puffball","health":10,"damage":1,"size":1,"speed":0,"range":300,"cooldown":500,"color":"#FFFFFF","section":[0],"ai_type":"passive","petal_ring":{"petalType":"pearl","count":4,"spin":false,"shootOnHit":true}},
  "centipede": {"name":"Centipede","health":10,"damage":1,"size":1,"speed":0.2,"range":300,"cooldown":500,"color":"#88AA00","section":[0],"ai_type":"passive"},
  "centipede_body": {"name":"Centipede Body","health":10,"damage":1,"size":1,"speed":0.2,"range":300,"cooldown":500,"color":"#88AA00","section":[0],"ai_type":"passive"},
  "matriarch": {"name":"Matriarch","health":10,"damage":1,"size":1,"speed":0.2,"range":300,"cooldown":500,"color":"#AA00AA","section":[0],"ai_type":"hostile","periodic_spawn":{"mobType":"critter","intervalMs":1000,"lifetimeMs":5000,"maxAlive":3,"spawnRarityOffset":-1}}
})JSON";

struct Fixture {
    ContentRegistry registry;
    bool ok = false;
    std::string error;
};

const Fixture& fixture() {
    static const Fixture state = [] {
        Fixture f;
        f.ok = test::loadFixtureContent(f.registry, testsupport::tempDir("flix_petal_tests"),
                                        kMobsJson, kPetalsJson, f.error);
        return f;
    }();
    return state;
}

bool contentLoaded() {
    if (!fixture().ok) {
        ::testing::reportFailure(__FILE__, __LINE__,
                                 "fixture content failed to load: " + fixture().error);
    }
    return fixture().ok;
}

std::uint16_t petalId(const char* id) { return fixture().registry.petalIndex(id); }

/// A world with one player, and the system driven at the real tick rate.
struct Rig {
    const ContentRegistry& registry;
    World world;
    CommandBuffer commands{world};
    PetalSystem system;
    /// Where the tick's one-shot visuals land. Only the lightning strike
    /// reports one, and it is the only way to see a strike at all: its damage
    /// is a field combat resolves later, and leaves nothing on this system.
    EventQueue events;
    Entity player = NULL_ENTITY;
    // Deliberately not zero: a timer that was never set reads as 0, and a
    // clock starting there would make that bug look like a working one.
    double now = 1000.0;

    /// Optional, and empty: a map with no walls, so a flower under movement
    /// travels exactly as far as its velocity says.
    Terrain terrain;
    MovementSystem movement;
    bool stepMovement = false;
    bool useHatchTerrain = false;

    explicit Rig(const ContentRegistry& content = fixture().registry) : registry(content) {
        player = world.create();
        world.add<PlayerTag>(player);
        world.add<Transform>(player, Transform{{1000.0, 1000.0}, 0.0});
        world.add<Motion>(player);
        world.add<Knockback>(player);
        world.add<Body>(player, Body{kPlayerBaseRadius, 1.0});
        world.add<Health>(player, Health{maxHealthForLevel(1), maxHealthForLevel(1), 0.0, 0.0});
        world.add<Faction>(player, Faction{Team::Players, false});
        world.add<PlayerProgress>(player, PlayerProgress{});
        world.add<PlayerInput>(player);
        world.add<PlayerModifiers>(player);
        world.add<PlayerLocation>(player);
        world.add<Loadout>(player);
        world.add<PetalRing>(player);
        world.add<Afflictions>(player);
        world.add<HitCooldowns>(player);
    }

    void equip(int slot, const char* id, Rarity rarity = Rarity::Common) {
        LoadoutSlot& s = world.get<Loadout>(player).slots[static_cast<std::size_t>(slot)];
        s.configIndex = registry.petalIndex(id);
        s.rarity = rarity;
    }

    void unequip(int slot) {
        LoadoutSlot& s = world.get<Loadout>(player).slots[static_cast<std::size_t>(slot)];
        s.configIndex = kNoPetal;
    }

    void setFlags(std::uint8_t flags) { world.get<PlayerInput>(player).current.flags = flags; }

    /// The direction the player is asking to travel in, as an input frame
    /// carries it. Strength 0 is a player asking for nothing.
    void setMove(double angle, double strength = 1.0) {
        net::InputFrame& frame = world.get<PlayerInput>(player).current;
        frame.moveAngle = angle;
        frame.moveStrength = strength;
    }

    /// Stops the ring turning, by equipping a petal whose rotationSpeed
    /// modifier is zero -- the reference sums those as `+= modifier - 1`, so
    /// one of them cancels the base rate exactly.
    ///
    /// A petal is not welded to its place on the ring: gardn's step carries it
    /// there (kPetalFriction), so on a turning ring it settles into a small
    /// fixed lag and a slightly wider orbit and never sits on its target point.
    /// A test about WHERE the ring puts a petal freezes the ring first and then
    /// lets the step arrive; a test about the turning itself does not, and
    /// asserts against the steady state instead.
    ///
    /// It costs the LAST active slot, and one place on the ring with it.
    static constexpr int kAnchorSlot = kLoadoutActiveSlots - 1;
    void freezeRing() { equip(kAnchorSlot, "anchor"); }

    /// Steps until the ring step has carried every petal onto its target. With
    /// a frozen ring the target does not move, so this converges to the point
    /// itself rather than to an orbit around it. Eight seconds: gardn's swing
    /// keeps 0.794 of itself a tick, which this leaves far below a trillionth.
    void settleRing(int ticks = 240) { tick(ticks); }

    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            now += net::kTickMillis;
            // The server's order, for the tests that ask for it: the flower is
            // moved, then its ring is stepped. A petal that hands the flower
            // momentum -- the bubble -- is only observable through the
            // movement that spends it.
            if (stepMovement) movement.runPlayerPhase(world, terrain, now, net::kTickSeconds);
            system.run(world, registry, now, net::kTickSeconds, commands, useHatchTerrain ? &terrain : nullptr,
                       &events);
            commands.flush();
        }
    }

    /// Runs player movement alongside the ring. Off by default: a test about
    /// where the ring puts a petal wants a flower that holds still.
    void withMovement() { stepMovement = true; }

    /// The server's end-of-tick reaper: everything marked Dead is destroyed
    /// before the next tick's ring pass runs. tick() leaves it out, so a
    /// petal killed there is still around to be seen next tick -- which is
    /// never true in production, where combat marks the kill and the reaper
    /// takes it the same tick.
    void reap() {
        Query<Dead> dead{world};
        std::vector<Entity> doomed;
        dead.collect(doomed);
        for (const Entity e : doomed) world.destroy(e);
    }

    /// Steps until `done` holds, so a test can wait on a reload without
    /// hard-coding how many ticks that is.
    template <class F>
    bool tickUntil(F done, int maxTicks = 2000) {
        for (int i = 0; i < maxTicks; ++i) {
            tick();
            if (done()) return true;
        }
        return false;
    }

    /// Equipping a petal puts its slot on a full reload before the petal ever
    /// appears -- the reference's `updateLoadout` stamps `onCooldown` and a
    /// `cooldownEndTime` on every newly equipped slot, and the spawn loadout is
    /// built the same way. A test whose subject is something else waits that
    /// out here instead of restating each petal's cooldown in ticks.
    void settleEquips(int maxTicks = 400) {
        // One tick first: `broken` reads false until the system has actually
        // seen the edit and stamped the slot's reload, so testing it before
        // that would call an unserved cooldown "ready".
        tick();
        const auto ready = [&] {
            for (int i = 0; i < kLoadoutActiveSlots; ++i) {
                if (slot(i).empty()) continue;
                if (slot(i).broken) return false;
            }
            return true;
        };
        if (ready()) return;
        if (!tickUntil(ready, maxTicks)) {
            ::testing::reportFailure(__FILE__, __LINE__,
                                     "the equip reload never finished");
        }
    }

    const LoadoutSlot& slot(int index) const {
        return const_cast<World&>(world).get<Loadout>(player).slots[static_cast<std::size_t>(index)];
    }

    /// What the loadout bar drains its tile by: how much of the slot is still
    /// standing, whichever of the two health models it runs on.
    double slotHealth(int index) const {
        return const_cast<World&>(world)
            .get<PetalSlotState>(player)
            .slots[static_cast<std::size_t>(index)]
            .healthFraction;
    }

    /// The number the loadout bar prints inside the slot's top border, and -1
    /// for a petal that has none.
    double slotCounter(int index) const {
        return const_cast<World&>(world)
            .get<PetalSlotState>(player)
            .slots[static_cast<std::size_t>(index)]
            .counter;
    }

    const PetalRing& ring() const { return const_cast<World&>(world).get<PetalRing>(player); }
    const PlayerModifiers& modifiers() const {
        return const_cast<World&>(world).get<PlayerModifiers>(player);
    }

    std::vector<Entity> petals(int slotIndex = -1) {
        std::vector<Entity> out;
        Query<PetalTag, PetalInstance> query{world};
        query.each([&](Entity e, PetalTag&, PetalInstance& instance) {
            if (instance.owner != player) return;
            if (slotIndex >= 0 && instance.slot != slotIndex) return;
            out.push_back(e);
        });
        std::sort(out.begin(), out.end(), [&](Entity a, Entity b) {
            return world.get<PetalInstance>(a).subIndex < world.get<PetalInstance>(b).subIndex;
        });
        return out;
    }

    Entity petalWithSub(int slotIndex, int subIndex) {
        for (const Entity e : petals(slotIndex)) {
            if (world.get<PetalInstance>(e).subIndex == subIndex) return e;
        }
        return NULL_ENTITY;
    }

    std::size_t countOf(net::EntityKind kind) {
        std::size_t n = 0;
        Query<Replicated> query{world};
        query.each([&](Entity, Replicated& replicated) {
            if (replicated.kind == kind) ++n;
        });
        return n;
    }

    std::size_t petCount() {
        Query<Pet> query{world};
        return query.count();
    }

    Vec2 position(Entity e) { return world.get<Transform>(e).position; }
    Vec2 velocity(Entity e) { return world.get<Motion>(e).velocity; }
    double radiusOf(Entity petal) { return (position(petal) - position(player)).length(); }
    double angleOf(Entity petal) { return (position(petal) - position(player)).angle(); }
    double healthOf(Entity e) { return world.get<Health>(e).current; }
    void damage(Entity e, double amount) { world.get<Health>(e).current -= amount; }
};

/// Angular distance, so a comparison across the -pi/+pi seam is not a failure.
double angularGap(double a, double b) { return std::fabs(wrapAngle(a - b)); }

/// gardn's three orbits for a new flower: its radius plus each state's gap.
constexpr double kRestOrbit = kPlayerBaseRadius + kPetalOrbitRestGap;
constexpr double kAttackOrbit = kPlayerBaseRadius + kPetalOrbitAttackGap;
constexpr double kDefendOrbit = kPlayerBaseRadius + kPetalOrbitDefendGap;

} // namespace

// ---------------------------------------------------------------------------
// Ring placement
// ---------------------------------------------------------------------------

static double feedbackNaturalReload(const char* id,Rarity rarity=Rarity::Common) {
    return fixture().registry.petalStats(petalId(id),rarity).reloadMillis*kPetalReloadFeelScale;
}
static double feedbackEquipReload(const char* id,Rarity rarity=Rarity::Common) {
    return std::max(feedbackNaturalReload(id,rarity),SkillSet{}.equipReloadFloorMillis());
}

TEST(petal_ring_places_one_petal_per_slot_evenly) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(3, "basic");
    rig.equip(7, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const std::vector<Entity> petals = rig.petals();
    CHECK_EQ(petals.size(), std::size_t(4));   // three basics and the anchor

    // Slots 0, 3, 7 and 9 are the first, second, third and fourth occupied
    // slots, so they take quarters of the ring regardless of the empty slots
    // between them.
    const double wedge = kTau / 4.0;
    const double spin = rig.ring().spin;
    for (const Entity petal : petals) {
        const int slot = rig.world.get<PetalInstance>(petal).slot;
        const int ordinal = slot == 0 ? 0 : (slot == 3 ? 1 : (slot == 7 ? 2 : 3));
        CHECK_NEAR(angularGap(rig.angleOf(petal), spin + wedge * ordinal), 0.0, 1e-6);
        CHECK_NEAR(rig.radiusOf(petal), rig.ring().radius, 1e-6);
    }
}

TEST(petal_ring_rotates_at_the_spin_rate) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(1, "basic");
    rig.settleEquips();
    // No freeze here -- the turning IS the subject. The spring settles into a
    // steady state on the moving ring: a fixed lead angle and a fixed slightly
    // wider orbit, both constant from then on. What must hold in that state is
    // that the petal keeps station with the ring rather than drifting round it.
    rig.settleRing();

    const Entity first = rig.petals(0).front();
    const double before = rig.angleOf(first);
    const double radiusBefore = rig.radiusOf(first);
    const double spinBefore = rig.ring().spin;

    rig.tick(5);
    const double expected = kPetalSpinRate * net::kTickSeconds * 5.0;
    CHECK_NEAR(angularGap(rig.ring().spin, spinBefore + expected), 0.0, 1e-9);
    CHECK_NEAR(angularGap(rig.angleOf(rig.petals(0).front()), before + expected), 0.0, 1e-9);
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()), radiusBefore, 1e-6);

    // The gap between the two petals is fixed by the layout: rotation must
    // carry the whole ring, not slide one petal along it.
    CHECK_NEAR(angularGap(rig.angleOf(rig.petals(0).front()), rig.angleOf(rig.petals(1).front())),
               kPi, 1e-9);
}

/// The ring step is gardn's, restated for our 30 Hz tick: a petal knocked off
/// its point travels back the way gardn's would, second for second. gardn's
/// own step is run beside it at its own 20 Hz, and the two are compared at
/// every instant both clocks share (every tenth of a second).
TEST(the_ring_step_moves_a_petal_as_gardns_does) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 point = rig.position(petal);
    const Vec2 out = (point - rig.position(rig.player)).normalized();
    const double knock = 100.0;
    rig.world.get<Transform>(petal).position = point + out * knock;

    // gardn, Server/Process/Flower.cc and Motion.cc, per 20 Hz tick: friction
    // DEFAULT_FRICTION * 1.5, acceleration half the gap.
    double gardnGap = knock;
    double gardnVelocity = 0.0;
    double deepest = 0.0;
    double worst = 0.0;
    const int comparisonTicks = net::kTicksPerSecond / 10;
    for (int tick = 1; tick <= net::kTicksPerSecond; ++tick) {
        rig.tick();
        if (tick % comparisonTicks != 0) continue;
        for (int step = 0; step < 2; ++step) {
            gardnVelocity *= 1.0 - 0.5;
            gardnVelocity += 0.5 * (0.0 - gardnGap);
            gardnGap += gardnVelocity;
        }
        const Vec2 off = rig.position(petal) - point;
        const double ours = off.x * out.x + off.y * out.y;
        deepest = std::min(deepest, ours);
        // The first tenth of a second is left out: the two clocks start the
        // swing on different phases of a tick, which is all that separates
        // them, and by the fifth of a second they have met.
        if (tick >= comparisonTicks * 2) worst = std::max(worst, std::fabs(ours - gardnGap));
    }
    CHECK(worst < knock * 0.025);
    // And it is gardn's swing, not a settle: it goes a quarter past the point.
    CHECK(deepest < -knock * 0.2);
}

/// The buttons set the ring at once to one of gardn's three states, and the
/// petals travel to it on the ring step: out past the extended ring and back,
/// rather than gliding up to it.
TEST(attacking_and_defending_set_the_ring_and_the_petals_travel_to_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();

    // gardn: `player.radius + 40`, `+ 100` attacking, `+ 15` defending.
    const double rest = kRestOrbit;
    CHECK_NEAR(rest, 65.0, 1e-9);
    CHECK_NEAR(rig.ring().radius, rest, 1e-9);

    rig.settleRing();
    // On a turning ring the step holds the petal a fixed fraction outside its
    // point, chasing it round; about a hundredth at the base spin rate.
    const double restOrbitRatio = rig.radiusOf(rig.petals(0).front()) / rig.ring().radius;
    CHECK_NEAR(restOrbitRatio, 1.0, 0.02);

    rig.setFlags(net::InputAttack);
    rig.tick();
    const double attackTarget = kAttackOrbit;
    CHECK_NEAR(attackTarget, 125.0, 1e-9);
    CHECK_NEAR(rig.ring().radius, attackTarget, 1e-9);
    // The ring is there; the petal is on its way, not teleported.
    const double afterOne = rig.radiusOf(rig.petals(0).front());
    CHECK(afterOne > rest * restOrbitRatio);
    CHECK(afterOne < attackTarget);
    double furthest = afterOne;
    for (int i = 0; i < 30; ++i) {
        rig.tick();
        furthest = std::max(furthest, rig.radiusOf(rig.petals(0).front()));
    }
    CHECK(furthest > attackTarget * 1.05);

    rig.tick(200);
    // It followed the ring out: it orbits at the same proportion of the
    // extended radius that it did of the resting one.
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()) / rig.ring().radius, restOrbitRatio, 1e-6);

    rig.setFlags(net::InputDefend);
    rig.tick();
    const double defendTarget = kDefendOrbit;
    CHECK_NEAR(defendTarget, 40.0, 1e-9);
    CHECK_NEAR(rig.ring().radius, defendTarget, 1e-9);
    const double afterDefend = rig.radiusOf(rig.petals(0).front());
    CHECK(afterDefend < attackTarget * restOrbitRatio);
    CHECK(afterDefend > defendTarget);
    rig.tick(200);
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()) / rig.ring().radius, restOrbitRatio, 1e-6);

    // Both buttons at once is a lunge, not a block. The reference tests the
    // extend button FIRST -- `if (extendPressed) ... else if (retractPressed)`
    // -- and only reaches the retract branch when extend is up, so a player
    // holding both throws the ring out rather than pulling it in.
    rig.setFlags(net::InputAttack | net::InputDefend);
    rig.tick();
    CHECK_NEAR(rig.ring().radius, attackTarget, 1e-9);
}

/// gardn's third eye: extra reach on the ATTACKING ring and nowhere else, and
/// a second one on the bar adds nothing, since gardn sets the figure.
TEST(an_attack_range_modifier_reaches_further_only_while_attacking) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "reacher");
    rig.tick();
    CHECK_NEAR(rig.ring().radius, kRestOrbit, 1e-9);
    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK_NEAR(rig.ring().radius, kDefendOrbit, 1e-9);
    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK_NEAR(rig.ring().radius, kAttackOrbit + 50.0, 1e-9);

    rig.equip(1, "reacher");
    rig.tick();
    CHECK_NEAR(rig.ring().radius, kAttackOrbit + 50.0, 1e-9);
}

TEST(a_player_radius_modifier_scales_the_body_and_preserves_petal_clearance) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "inflator");
    rig.tick();

    const double playerRadius = rig.world.get<Body>(rig.player).radius;
    CHECK_NEAR(playerRadius, kPlayerBaseRadius * 1.5, 1e-9);
    // TypeScript grows the neutral ring from 60 to 70, not to 90: the extra
    // body radius is added once, preserving the gap from the flower's edge.
    CHECK_NEAR(rig.ring().radius,
               playerRadius + kPetalOrbitRestGap, 1e-9);
}

TEST(a_clumped_slot_spawns_a_cluster_around_one_ring_position) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const std::vector<Entity> grains = rig.petals(0);
    CHECK_EQ(grains.size(), std::size_t(4));

    // All four hang off the SAME ring position -- that is what clumped means.
    const double reach = rig.ring().radius;
    const Vec2 slotPoint = rig.position(rig.player) + Vec2::fromAngle(rig.ring().spin, reach);
    // `effectiveSize * 40 * 0.5` in the reference, which is exactly the
    // petal's own hit radius -- the grains sit one radius out from the shared
    // centre, not one diameter.
    const double spacing = fixture().registry.petalStats(petalId("sandy"), Rarity::Common).radius;

    std::vector<double> subAngles;
    for (const Entity grain : grains) {
        CHECK_NEAR((rig.position(grain) - slotPoint).length(), spacing, 1e-6);
        CHECK_EQ(int(rig.world.get<PetalInstance>(grain).subCount), 4);
        subAngles.push_back((rig.position(grain) - slotPoint).angle());
    }
    // Four distinct directions out of the cluster centre, not four petals piled
    // on the same point.
    std::sort(subAngles.begin(), subAngles.end());
    for (std::size_t i = 1; i < subAngles.size(); ++i) {
        CHECK_NEAR(subAngles[i] - subAngles[i - 1], kTau / 4.0, 1e-6);
    }
}

/// gardn's clumps: every clump on the ring points its grains the same way in
/// the world, whatever its place on the ring, and that bearing turns at a
/// fifth of the ring's own rate.
TEST(clumps_point_their_grains_one_way_and_turn_at_a_fifth_of_the_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.equip(1, "sandy");
    rig.settleEquips();
    rig.settleRing();

    // The same grain on two clumps half a ring apart faces the same way.
    for (int sub = 0; sub < 4; ++sub) {
        const double a = rig.world.get<PetalInstance>(rig.petalWithSub(0, sub)).facingAngle;
        const double b = rig.world.get<PetalInstance>(rig.petalWithSub(1, sub)).facingAngle;
        CHECK_NEAR(angularGap(a, b), 0.0, 1e-9);
        CHECK_NEAR(angularGap(a, rig.ring().clumpSpin + kTau * sub / 4.0), 0.0, 1e-9);
    }

    const double spin = rig.ring().spin;
    const double clump = rig.ring().clumpSpin;
    rig.tick(6);
    const double ringTurn = wrapAngle(rig.ring().spin - spin);
    CHECK(ringTurn > 0.0);
    CHECK_NEAR(wrapAngle(rig.ring().clumpSpin - clump), ringTurn * kPetalClumpSpinShare, 1e-9);
}

TEST(a_clump_spacing_moves_the_grains_that_many_radii_out) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "spready");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const std::vector<Entity> grains = rig.petals(0);
    CHECK_EQ(grains.size(), std::size_t(3));

    const Vec2 slotPoint =
        rig.position(rig.player) + Vec2::fromAngle(rig.ring().spin, rig.ring().radius);
    const double radius = fixture().registry.petalStats(petalId("spready"), Rarity::Common).radius;
    for (const Entity grain : grains) {
        CHECK_NEAR((rig.position(grain) - slotPoint).length(), radius * 2.5, 1e-6);
    }
}

TEST(a_clump_outside_the_ring_puts_one_grain_on_the_orbit_and_the_rest_beyond) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "hanging");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const std::vector<Entity> grains = rig.petals(0);
    CHECK_EQ(grains.size(), std::size_t(3));

    const Vec2 flower = rig.position(rig.player);
    const double orbit = rig.ring().radius;
    const double spacing =
        fixture().registry.petalStats(petalId("hanging"), Rarity::Common).radius * 2.5;
    // The clump's centre is one spacing outside the orbit, on the slot's bearing.
    const Vec2 hub = flower + Vec2::fromAngle(rig.ring().spin, orbit + spacing);

    int onOrbit = 0;
    for (const Entity grain : grains) {
        CHECK_NEAR((rig.position(grain) - hub).length(), spacing, 1e-6);
        const double out = (rig.position(grain) - flower).length();
        if (std::fabs(out - orbit) < 1e-6) {
            ++onOrbit;
        } else {
            CHECK(out > orbit + spacing);
        }
    }
    CHECK_EQ(onOrbit, 1);
}

TEST(an_empty_loadout_places_nothing_and_leaves_modifiers_neutral) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.tick(5);
    CHECK_EQ(rig.petals().size(), std::size_t(0));
    CHECK_NEAR(rig.modifiers().speedScale, 1.0, 1e-12);
    CHECK_NEAR(rig.modifiers().luck, 1.0, 1e-12);
    CHECK_NEAR(rig.ring().radius, kRestOrbit, 1e-9);
}

TEST(a_worn_cutter_adds_body_damage_and_takes_no_ring_place) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.world.add<ContactDamage>(rig.player, ContactDamage{0.0, 0.0});
    rig.equip(0, "blade", Rarity::Epic);
    rig.equip(1, "basic");
    rig.settleEquips();

    const int level = rig.world.get<PlayerProgress>(rig.player).level;
    const auto granted = [&](Rarity rarity) {
        return fixture().registry.petalStats(petalId("blade"), rarity).bodyDamage;
    };

    // A cutter is carried, not swung: it spawns nothing, so the basic petal is
    // the ring's only occupant and gets the whole circle to itself.
    CHECK_EQ(rig.petals().size(), std::size_t(1));
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    // The petal damage ladder: 3x a tier from the authored 10, so an epic
    // blade grants 270. gardn's flat +20 does not survive a ten-tier game.
    CHECK_NEAR(granted(Rarity::Epic), 270.0, 1e-9);
    CHECK_NEAR(rig.modifiers().bodyDamageBonus, granted(Rarity::Epic), 1e-12);
    CHECK_NEAR(rig.world.get<ContactDamage>(rig.player).amount,
               bodyDamageForLevel(level) + granted(Rarity::Epic), 1e-9);

    // Rarity moves it, on the same 3x ladder a petal's damage climbs.
    CHECK(granted(Rarity::Common) < granted(Rarity::Epic));
    CHECK(granted(Rarity::Epic) < granted(Rarity::Unique));
    CHECK_NEAR(granted(Rarity::Epic), 27.0 * granted(Rarity::Common), 1e-9);
    CHECK_NEAR(granted(Rarity::Unique), 6561.0 * granted(Rarity::Common), 1e-9);
    // Exactly a basic petal's hit at the same tier: the anchor the authored
    // value is chosen for.
    CHECK_NEAR(granted(Rarity::Epic),
               fixture().registry.petalStats(petalId("basic"), Rarity::Epic).damage, 1e-9);

    // Maximised, never summed: a second blade cannot double the bonus, and the
    // better of the two is the one that counts.
    rig.equip(2, "sparkblade", Rarity::Common);
    rig.tick(5);
    CHECK_NEAR(rig.modifiers().bodyDamageBonus, granted(Rarity::Epic), 1e-12);
    rig.equip(2, "sparkblade", Rarity::Unique);
    rig.tick(5);
    CHECK_NEAR(rig.modifiers().bodyDamageBonus, granted(Rarity::Unique), 1e-12);

    rig.unequip(0);
    rig.unequip(2);
    rig.tick(5);
    CHECK_NEAR(rig.modifiers().bodyDamageBonus, 0.0, 1e-12);
    CHECK_NEAR(rig.world.get<ContactDamage>(rig.player).amount, bodyDamageForLevel(level), 1e-9);
}

TEST(petals_worn_on_the_flower_leave_no_gap_in_the_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "blade");
    rig.equip(2, "basic");
    rig.equip(4, "gazer");
    rig.equip(5, "sparkblade");
    rig.equip(7, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    // The two blades and the eye sit on the flower, so the two basics and the
    // anchor are the whole ring and split it in thirds -- not sixths with
    // three empty places where the worn petals were counted.
    const std::vector<Entity> petals = rig.petals();
    CHECK_EQ(petals.size(), std::size_t(3));
    const double wedge = kTau / 3.0;
    const double spin = rig.ring().spin;
    for (const Entity petal : petals) {
        const int slot = rig.world.get<PetalInstance>(petal).slot;
        const int ordinal = slot == 2 ? 0 : (slot == 7 ? 1 : 2);
        CHECK_NEAR(angularGap(rig.angleOf(petal), spin + wedge * ordinal), 0.0, 1e-6);
        CHECK_NEAR(rig.radiusOf(petal), rig.ring().radius, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// Attraction
// ---------------------------------------------------------------------------

namespace {

/// A still, hard-to-kill mob of body radius `radius` at `at`, of the kind the
/// attraction tests pull petals toward.
Entity addAttractor(Rig& rig, Vec2 at, double radius, double jitter = 1.0) {
    const Entity mob = rig.world.create();
    rig.world.add<MobTag>(mob);
    rig.world.add<MobType>(mob, MobType{fixture().registry.mobIndex("brute"), Rarity::Common, jitter});
    rig.world.add<Transform>(mob, Transform{at, 0.0});
    rig.world.add<Body>(mob, Body{radius, 1.0});
    rig.world.add<Health>(mob, Health{1e9, 1e9, 0.0, 0.0});
    rig.world.add<Faction>(mob, Faction{Team::Hostiles, false});
    return mob;
}

} // namespace

/// Settles a frozen ring's one basic petal, puts a mob `clear` units off its
/// ring point along the way out (negative: the point is inside the mob), lets
/// the petal go where the mob takes it, and reports where it came to rest. A
/// frozen ring does not turn, so neither does the petal's point on the mob.
struct AttractionRest {
    Vec2 orbit;
    Vec2 rest;
    Vec2 mob;
    double petalRadius = 0;
    Entity attractedTo = NULL_ENTITY;
    Entity mobEntity = NULL_ENTITY;
};
AttractionRest restBeside(double clear, double mobRadius, double jitter = 1.0) {
    Rig rig;
    rig.equip(0, "basic");
    rig.freezeRing();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    AttractionRest out;
    out.orbit = rig.position(petal);
    out.petalRadius = rig.world.get<Body>(petal).radius;
    const Vec2 away = (out.orbit - rig.position(rig.player)).normalized();
    out.mob = out.orbit + away * (clear + mobRadius);
    out.mobEntity = addAttractor(rig, out.mob, mobRadius, jitter);
    rig.tick(240);
    out.rest = rig.position(petal);
    out.attractedTo = rig.world.get<PetalInstance>(petal).attractedTo;
    return out;
}

/// A mob whose edge is in reach of a petal's ring point takes the petal onto
/// that edge -- the edge of the BODY the hit test uses, so a mob that rolled
/// small is grazed too -- at the petal's own bearing from it.
TEST(a_mob_in_reach_draws_a_petal_onto_its_edge) {
    if (!contentLoaded()) return;
    const std::uint16_t brute = fixture().registry.mobIndex("brute");
    const double jitter = 0.55;
    const double small = fixture().registry.mobStats(brute, Rarity::Common).radius * jitter;
    for (const double clear : {10.0, 25.0}) {
        const AttractionRest r = restBeside(clear, small, jitter);
        CHECK(r.attractedTo == r.mobEntity);
        CHECK_NEAR((r.rest - r.mob).length(), small, 1e-6);
        // On the near side, where it came from.
        CHECK(angularGap((r.rest - r.mob).angle(), (r.orbit - r.mob).angle()) < 1e-6);
    }
}

/// A petal whose ring point is buried in a mob is put on its edge too.
TEST(a_petal_inside_a_mob_is_put_on_its_edge) {
    if (!contentLoaded()) return;
    // The ring point ten units inside a mob of radius 20.
    const AttractionRest r = restBeside(-10.0, 20.0);
    CHECK_NEAR((r.rest - r.mob).length(), 20.0, 1e-6);
}

/// A ring point already on a mob's edge stays where it is, and a mob whose
/// edge is out of reach of it takes nothing.
TEST(a_mob_moves_nothing_on_its_edge_or_beyond_its_reach) {
    if (!contentLoaded()) return;
    const AttractionRest atEdge = restBeside(0.0, 20.0);
    CHECK_NEAR((atEdge.rest - atEdge.orbit).length(), 0.0, 1e-6);
    const double reach = PlayerModifiers{}.petalAttractionRadius;
    const AttractionRest outOfReach = restBeside(reach + 5.0, 20.0);
    CHECK_NEAR((outOfReach.rest - outOfReach.orbit).length(), 0.0, 1e-6);
    CHECK(outOfReach.attractedTo == NULL_ENTITY);
}

/// The whole pass on a turning ring, for a small, a middling and a big mob:
/// the petal leaves the ring for the mob, circles its edge against the ring's
/// turn -- all the way round a small one -- for as long as its ring point is
/// in reach, and goes back onto the ring once it has turned on. It is the
/// ring's own step aimed at the mob, and the circle runs with the ring where
/// the two meet, so nothing in it jolts.
TEST(a_petal_orbits_a_mob_its_ring_passes_and_goes_back_after) {
    if (!contentLoaded()) return;
    for (const double radius : {10.0, 30.0, 100.0}) {
        Rig rig;
        rig.equip(0, "basic");
        rig.settleEquips();
        rig.settleRing();
        const Entity petal = rig.petals(0).front();
        const double petalRadius = rig.world.get<Body>(petal).radius;
        const Vec2 centre = rig.position(rig.player);
        // Half a turn ahead of the petal, five units outside its ring.
        const double ahead = rig.angleOf(petal) + kPi;
        const Vec2 at = centre + Vec2::fromAngle(ahead, kRestOrbit + 5.0 + radius);
        addAttractor(rig, at, radius);

        Vec2 last = rig.position(petal);
        Vec2 lastVelocity{};
        double worst = 0.0;
        double around = 0.0;
        double lastBearing = (rig.position(petal) - at).angle();
        int touching = 0;
        for (int i = 0; i < 60; ++i) {
            rig.tick();
            const Vec2 velocity = (rig.position(petal) - last) / net::kTickSeconds;
            if (i > 0) worst = std::max(worst, (velocity - lastVelocity).length() / net::kTickSeconds);
            last = rig.position(petal);
            lastVelocity = velocity;
            const double bearing = (rig.position(petal) - at).angle();
            if (rig.world.get<PetalInstance>(petal).attractedTo != NULL_ENTITY) {
                around += wrapAngle(bearing - lastBearing);
            }
            lastBearing = bearing;
            if ((rig.position(petal) - at).length() < radius + petalRadius) ++touching;
        }
        CHECK(touching * net::kTickSeconds > 0.4);
        // The ring turns the positive way; the circle, the other.
        CHECK(around < 0.0);
        if (radius == 10.0) CHECK(around < -2.0 * kPi * 0.8);
        CHECK(worst < 20000.0);
        // Gone on round the ring, and not yet back to the mob: on the ring.
        rig.tick(30);
        CHECK_NEAR(rig.radiusOf(petal), kRestOrbit, 1.0);
    }
}

/// A pile of mobs on one spot takes a petal as one: it circles one of them and
/// is not thrown about by the rest.
TEST(a_pile_of_mobs_holds_a_petal_without_flinging_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 centre = rig.position(rig.player);
    const double radius = 20.0;
    const Vec2 at = centre + Vec2::fromAngle(rig.angleOf(petal) + 0.5, kRestOrbit + 5.0 + radius);
    for (int i = 0; i < 5; ++i) addAttractor(rig, at, radius);

    Vec2 last = rig.position(petal);
    Vec2 lastVelocity{};
    double worst = 0.0;
    double farthest = 0.0;
    for (int i = 0; i < 90; ++i) {
        rig.tick();
        const Vec2 velocity = (rig.position(petal) - last) / net::kTickSeconds;
        if (i > 0) worst = std::max(worst, (velocity - lastVelocity).length() / net::kTickSeconds);
        last = rig.position(petal);
        lastVelocity = velocity;
        farthest = std::max(farthest, rig.radiusOf(petal));
    }
    CHECK(worst < 20000.0);
    CHECK(farthest < kRestOrbit + 5.0 + radius * 2.0 + 20.0);
}

TEST(a_defend_only_petal_is_never_attracted) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "cotton");
    rig.freezeRing();
    rig.settleRing();

    const Entity petal = rig.petals(0).front();
    // The test means nothing for a petal that skips the spring step entirely.
    const PetalConfig& config = fixture().registry.petal(petalId("cotton"));
    CHECK(config.defendOnly);
    CHECK(!config.noPhysics && !config.hasFixedDirection);

    // A mob whose edge is 10 units off the orbit point, which draws a basic
    // petal onto it (the tests above): well inside the base attraction radius.
    const Vec2 orbit = rig.world.get<Transform>(petal).position;
    const Entity mob = rig.world.create();
    rig.world.add<MobTag>(mob);
    rig.world.add<MobType>(mob, MobType{fixture().registry.mobIndex("brute"), Rarity::Common, 1.0});
    rig.world.add<Transform>(mob, Transform{orbit + Vec2{20.0, 0.0}, 0.0});
    rig.world.add<Body>(mob, Body{10.0, 1.0});
    rig.world.add<Health>(mob, Health{500.0, 500.0, 0.0, 0.0});
    rig.world.add<Faction>(mob, Faction{Team::Hostiles, false});

    rig.tick(60);

    CHECK(rig.world.get<PetalInstance>(petal).attractedTo == NULL_ENTITY);
    CHECK_NEAR((rig.world.get<Transform>(petal).position - orbit).length(), 0.0, 1e-3);
}

// ---------------------------------------------------------------------------
// Lightning
// ---------------------------------------------------------------------------

/// PetalSystem's own kLightningRadius, which is private to it. Spelling the
/// number out here is deliberate: a test that hard-codes it is what notices the
/// reach changing under a strike that is supposed to cover a screen.
constexpr double kStrikeRadius = 1000.0;

/// Puts a mob on the field with enough health to survive the strike, so what is
/// being measured is the report and not the corpse.
Entity addMob(Rig& rig, Vec2 at, const char* type = "critter") {
    const std::uint16_t index = rig.registry.mobIndex(type);
    const Entity mob = rig.world.create();
    rig.world.add<MobTag>(mob);
    rig.world.add<MobType>(mob, MobType{index, Rarity::Common, 1.0});
    rig.world.add<Transform>(mob, Transform{at, 0.0});
    rig.world.add<Body>(mob, Body{10.0, 1.0});
    rig.world.add<Health>(mob, Health{10000.0, 10000.0, 0.0, 0.0});
    rig.world.add<Faction>(mob, Faction{Team::Hostiles, false});
    return mob;
}

void ricochetCombatTick(Rig& rig, CombatSystem& combat) {
    SpatialGrid grid;
    Query<Transform, Body> bodies{rig.world};
    bodies.each([&](Entity e, Transform& at, Body& body) {
        grid.insert(e, at.realm, at.position, body.radius);
    });
    combat.run(rig.world, grid, rig.registry, rig.now, net::kTickSeconds,
               rig.commands, rig.events);
    rig.commands.flush();
}

TEST(icosahedron_ricochet_hits_at_most_four_distinct_mobs_once_then_returns) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 start = rig.position(petal);
    const Entity first = addMob(rig, start);
    const Entity second = addMob(rig, start + Vec2{80, 0});
    const Entity third = addMob(rig, start + Vec2{160, 0});
    const Entity fourth = addMob(rig, start + Vec2{240, 0});
    const Entity fifth = addMob(rig, start + Vec2{320, 0});
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.world.get<PetalInstance>(petal).ricochet.active);
    double furthestTravel = 0.0;
    CHECK(rig.tickUntil([&] {
        furthestTravel = std::max(furthestTravel, distance(rig.position(petal), start));
        ricochetCombatTick(rig, combat);
        return !rig.world.get<PetalInstance>(petal).ricochet.active;
    }, 120));
    CHECK_NEAR(rig.world.get<Health>(first).current, 9990.0, 1e-9);
    CHECK_NEAR(rig.world.get<Health>(second).current, 9992.5, 1e-9);
    CHECK_NEAR(rig.world.get<Health>(third).current, 9994.375, 1e-9);
    CHECK_NEAR(rig.world.get<Health>(fourth).current, 9995.78125, 1e-9);
    CHECK_NEAR(rig.world.get<Health>(fifth).current, 10000.0, 1e-9);
    CHECK(furthestTravel >= 200.0);
    CHECK(distanceSq(rig.position(petal), start) < 1.0);
    CHECK(rig.world.get<PetalInstance>(petal).ricochet.readyAtMillis > rig.now);
    CHECK_NEAR(rig.world.get<Health>(petal).current, 6.0, 1e-9);
    // Back on the ring, ordinary hits still work while the next chain charges.
    // First remove its old target so it cannot wear the petal down to a break.
    rig.world.destroy(first);
    const Entity solo = addMob(rig, rig.position(petal));
    for (int i = 0; i < 2; ++i) {
        rig.tick();
        ricochetCombatTick(rig, combat);
    }
    CHECK(rig.world.get<Health>(solo).current < 10000.0);
    CHECK(!rig.world.get<PetalInstance>(petal).ricochet.active);
}

TEST(icosahedron_ricochet_skips_invalid_targets_and_recovers_when_target_dies) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 start = rig.position(petal);
    const Entity first = addMob(rig, start);
    const Entity friendly = addMob(rig, start + Vec2{30, 0});
    rig.world.get<Faction>(friendly).team = Team::Players;
    const Entity dead = addMob(rig, start + Vec2{40, 0});
    rig.world.get<Health>(dead).current = 0;
    const Entity elsewhere = addMob(rig, start + Vec2{50, 0});
    rig.world.get<Transform>(elsewhere).realm = Realm::Arena;
    const Entity invulnerable = addMob(rig, start + Vec2{60, 0});
    rig.world.get<Health>(invulnerable).invulnerableUntilMillis = rig.now + 10000;
    const Entity tooFar = addMob(rig, start + Vec2{800, 0});
    const Entity target = addMob(rig, start + Vec2{150, 0});
    // Start from a confirmed first hit; placement should pick only the valid mob.
    rig.world.get<PetalInstance>(petal).ricochet.landed(first, rig.now);
    rig.tick();
    CHECK_EQ(rig.world.get<PetalInstance>(petal).ricochet.target, target);
    rig.world.destroy(target);
    rig.tick();
    if (rig.world.get<PetalInstance>(petal).ricochet.active) {
        CHECK(rig.world.get<PetalInstance>(petal).ricochet.returning);
        CHECK(rig.tickUntil([&] {
            return !rig.world.get<PetalInstance>(petal).ricochet.active;
        }, 120));
    }
    CHECK(distanceSq(rig.position(petal), start) < 1.0);
    CHECK_NEAR(rig.world.get<Health>(tooFar).current, 10000.0, 1e-9);
}

TEST(icosahedron_ricochet_only_hits_one_overlapping_mob_per_tick) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 start = rig.position(petal);
    const Entity a = addMob(rig, start);
    const Entity b = addMob(rig, start);
    const Entity c = addMob(rig, start);
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK_EQ(rig.world.get<PetalInstance>(petal).ricochet.hits, 1);
    CHECK_NEAR(rig.world.get<Health>(a).current + rig.world.get<Health>(b).current +
                   rig.world.get<Health>(c).current, 29990.0, 1e-9);
    CHECK(rig.tickUntil([&] {
        ricochetCombatTick(rig, combat);
        return !rig.world.get<PetalInstance>(petal).ricochet.active;
    }, 120));
    CHECK_NEAR(rig.world.get<Health>(a).current + rig.world.get<Health>(b).current +
                   rig.world.get<Health>(c).current, 29976.875, 1e-9);
}

TEST(icosahedron_guarantees_a_second_hit_against_lethal_contact_then_can_break) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 start = rig.position(petal);
    const Entity first = addMob(rig, start);
    const Entity second = addMob(rig, start + Vec2{100, 0});
    const Entity third = addMob(rig, start + Vec2{200, 0});
    rig.world.add<ContactDamage>(first, ContactDamage{10000});
    rig.world.add<ContactDamage>(second, ContactDamage{10000});
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(!rig.world.has<Dead>(petal));
    CHECK_NEAR(rig.world.get<Health>(petal).current, 1.0, 1e-9);
    CHECK(rig.world.get<PetalInstance>(petal).ricochet.protectSecondHit);
    // Incoming damage during the short flight cannot steal the promised hit.
    combat.applyDamage(rig.world, petal, first, 10000, rig.now, DamageKind::Poison);
    CHECK(!rig.world.has<Dead>(petal));
    CHECK(rig.tickUntil([&] {
        ricochetCombatTick(rig, combat);
        return rig.world.has<Dead>(petal);
    }, 60));
    CHECK_NEAR(rig.world.get<Health>(second).current, 9992.5, 1e-9);
    CHECK_NEAR(rig.world.get<Health>(third).current, 10000.0, 1e-9);
    CHECK(!rig.world.get<PetalInstance>(petal).ricochet.protectSecondHit);
}

TEST(icosahedron_does_not_gain_protection_when_only_one_enemy_is_in_range) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.settleEquips();
    const Entity petal = rig.petals(0).front();
    const Entity first = addMob(rig, rig.position(petal));
    rig.world.add<ContactDamage>(first, ContactDamage{10000});
    // A friendly mob nearby must not grant a free survival window.
    const Entity friendly = addMob(rig, rig.position(petal) + Vec2{100, 0});
    rig.world.get<Faction>(friendly).team = Team::Players;
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.world.has<Dead>(petal));
    CHECK(!rig.world.get<PetalInstance>(petal).ricochet.protectSecondHit);
}

TEST(icosahedron_second_hit_protection_ends_if_its_only_next_target_disappears) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Entity first = addMob(rig, rig.position(petal));
    const Entity second = addMob(rig, rig.position(petal) + Vec2{100, 0});
    rig.world.add<ContactDamage>(first, ContactDamage{10000});
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.world.get<PetalInstance>(petal).ricochet.protectSecondHit);
    rig.world.destroy(second);
    rig.tick();
    CHECK(!rig.world.get<PetalInstance>(petal).ricochet.protectSecondHit);
    combat.applyDamage(rig.world, petal, first, 10000, rig.now);
    CHECK(rig.world.has<Dead>(petal));
}

TEST(icosahedron_ricochet_breaks_and_reloads_with_fresh_flight_state) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.settleEquips();
    const Entity petal = rig.petals(0).front();
    rig.world.get<Health>(petal).current = 1.0;
    addMob(rig, rig.position(petal));
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.world.has<Dead>(petal));
    rig.reap();
    rig.tick();
    CHECK(rig.petals(0).empty());
    CHECK(rig.tickUntil([&] { return !rig.petals(0).empty(); }, 120));
    const auto& flight = rig.world.get<PetalInstance>(rig.petals(0).front()).ricochet;
    CHECK(flight.enabled);
    CHECK(!flight.active);
    CHECK_EQ(flight.hits, 0);
}

TEST(icosahedron_ricochet_times_out_against_an_unconfirmed_target) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.settleEquips();
    const Entity petal = rig.petals(0).front();
    const Entity first = addMob(rig, rig.position(petal));
    addMob(rig, rig.position(petal) + Vec2{100, 0});
    rig.world.get<PetalInstance>(petal).ricochet.landed(first, rig.now);
    // No combat tick ever confirms arrival: even an un-hittable chase must end.
    CHECK(rig.tickUntil([&] {
        return !rig.world.get<PetalInstance>(petal).ricochet.active;
    }, 120));
}

TEST(icosahedron_bounces_continue_when_each_hit_kills_its_stationary_target) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Vec2 start = rig.position(petal);
    const Entity a = addMob(rig, start);
    const Entity b = addMob(rig, start + Vec2{100, 0});
    const Entity c = addMob(rig, start + Vec2{200, 0});
    for (const Entity mob : {a, b, c}) rig.world.get<Health>(mob).current = 5.0;
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    rig.reap();
    CHECK(!rig.world.isAlive(a));
    CHECK(rig.tickUntil([&] {
        ricochetCombatTick(rig, combat);
        rig.reap();
        return !rig.world.get<PetalInstance>(petal).ricochet.active;
    }, 120));
    CHECK(!rig.world.isAlive(b));
    CHECK(!rig.world.isAlive(c));
    CHECK(distanceSq(rig.position(petal), start) < 1.0);
}

TEST(icosahedron_ricochet_rearms_after_cooldown_and_unequips_cleanly_in_flight) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity petal = rig.petals(0).front();
    const Entity first = addMob(rig, rig.position(petal));
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.world.get<PetalInstance>(petal).ricochet.active);
    CHECK(rig.tickUntil([&] {
        return !rig.world.get<PetalInstance>(petal).ricochet.active;
    }, 120));
    const double ready = rig.world.get<PetalInstance>(petal).ricochet.readyAtMillis;
    rig.world.destroy(first);
    CHECK(rig.tickUntil([&] { return rig.now >= ready; }, 120));
    const Entity next = addMob(rig, rig.position(petal));
    addMob(rig, rig.position(petal) + Vec2{100, 0});
    ricochetCombatTick(rig, combat);
    const auto& flight = rig.world.get<PetalInstance>(petal).ricochet;
    CHECK(flight.active);
    CHECK_EQ(flight.hits, 1);
    CHECK(flight.visitedMob(next));
    rig.tick();
    rig.unequip(0);
    rig.tick();
    CHECK(!rig.world.isAlive(petal));
    CHECK(rig.petals(0).empty());
}

const WireEvent* firstStrike(const Rig& rig) {
    for (const WireEvent& e : rig.events.events()) {
        if (e.kind == net::EventKind::Lightning) return &e;
    }
    return nullptr;
}

TEST(a_strike_reports_a_bolt_to_every_mob_it_hit) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "lightning");
    rig.settleEquips();
    rig.settleRing();

    // One mob against the ring, which is what arms the petal, and two more well
    // inside the strike's thousand-unit reach but nowhere near the petal. All
    // three are struck, so all three must be drawn.
    const Vec2 orbit = rig.world.get<Transform>(rig.petals(0).front()).position;
    addMob(rig, orbit);
    addMob(rig, {1400.0, 1000.0});
    addMob(rig, {1000.0, 1300.0});

    CHECK(rig.tickUntil([&] { return firstStrike(rig) != nullptr; }, 120));
    const WireEvent* strike = firstStrike(rig);
    CHECK(strike != nullptr);
    if (strike == nullptr) return;
    CHECK_EQ(strike->points.size(), std::size_t(3));
    CHECK(strike->positional);

    // A mob outside the reach is not drawn a bolt, because it was not hit.
    Rig far;
    far.equip(0, "lightning");
    far.settleEquips();
    far.settleRing();
    addMob(far, far.world.get<Transform>(far.petals(0).front()).position);
    addMob(far, {1000.0 + kStrikeRadius + 200.0, 1000.0});
    CHECK(far.tickUntil([&] { return firstStrike(far) != nullptr; }, 120));
    const WireEvent* nearOnly = firstStrike(far);
    CHECK(nearOnly != nullptr);
    if (nearOnly != nullptr) CHECK_EQ(nearOnly->points.size(), std::size_t(1));
}

TEST(a_strike_into_a_pile_keeps_the_nearest_bolts) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "lightning");
    rig.settleEquips();

    const Vec2 orbit = rig.world.get<Transform>(rig.petals(0).front()).position;
    addMob(rig, orbit);
    // Further than the cap can carry, laid out so that "nearest" and "whatever
    // order the archetype happens to hold" cannot be confused: the far half is
    // created FIRST, so a plain truncation would keep exactly the wrong ones.
    for (std::size_t i = 0; i < net::kMaxLightningTargets; ++i) {
        addMob(rig, {orbit.x + 600.0 + 5.0 * static_cast<double>(i), orbit.y});
    }
    for (std::size_t i = 0; i < net::kMaxLightningTargets; ++i) {
        addMob(rig, {orbit.x + 5.0 * static_cast<double>(i), orbit.y + 20.0});
    }

    CHECK(rig.tickUntil([&] { return firstStrike(rig) != nullptr; }, 120));
    const WireEvent* strike = firstStrike(rig);
    CHECK(strike != nullptr);
    if (strike == nullptr) return;
    CHECK_EQ(strike->points.size(), net::kMaxLightningTargets);
    for (const Vec2& point : strike->points) {
        CHECK((point - strike->position).length() < 600.0);
    }
}

// ---------------------------------------------------------------------------
// The battery
// ---------------------------------------------------------------------------

/// PetalSystem's own kBatteryCharges and kBatteryStrikeIntervalMillis, which
/// are private to it. Spelt out here for the reason kStrikeRadius is: a test
/// that hard-codes them is what notices a battery quietly growing a fourth
/// charge or losing its pacing.
constexpr int kBatteryCharges = 3;
constexpr double kBatteryIntervalMillis = 500.0;

std::size_t countStrikes(const Rig& rig) {
    std::size_t count = 0;
    for (const WireEvent& e : rig.events.events()) {
        if (e.kind == net::EventKind::Lightning) ++count;
    }
    return count;
}

/// A mob overlapping the FLOWER's body without sitting on its centre, which
/// touchesMob reads as a degenerate overlap rather than a hit.
Entity ramMob(Rig& rig) {
    const Vec2 at = rig.world.get<Transform>(rig.player).position;
    return addMob(rig, {at.x + kPlayerBaseRadius + 5.0, at.y});
}

/// The battery still ON the ring. Nothing in this rig reaps a Dead entity, so
/// a spent one is still a live handle the world query returns: what has left
/// the ring is what the slot pass has dropped from the loadout, which is
/// exactly the petals carrying Dead.
Entity liveBattery(Rig& rig, int slotIndex = 0) {
    for (const Entity petal : rig.petals(slotIndex)) {
        if (!rig.world.has<Dead>(petal)) return petal;
    }
    return NULL_ENTITY;
}

int batteryCharges(Rig& rig, int slotIndex = 0) {
    const Entity petal = liveBattery(rig, slotIndex);
    if (petal == NULL_ENTITY) return -1;
    return rig.world.get<PetalInstance>(petal).charges;
}

double lastStrikeDamage(Rig& rig) {
    double damage = -1.0;
    Query<GroundEffectTag, GroundEffect> query{rig.world};
    query.each([&](Entity, GroundEffectTag&, GroundEffect& effect) {
        if (effect.owner == rig.player) damage = effect.damagePerHit;
    });
    return damage;
}


TEST(a_battery_arrives_with_three_charges_and_spends_only_on_damage) {
    CHECK(contentLoaded()); Rig rig; rig.equip(0,"battery");rig.settleEquips();
    CHECK_EQ(batteryCharges(rig),3);const auto source=ramMob(rig);rig.tick(40);
    CHECK_EQ(countStrikes(rig),0u);CHECK_EQ(batteryCharges(rig),3);
    CombatSystem combat;combat.applyDamage(rig.world,rig.player,source,1,rig.now);rig.tick();
    CHECK_EQ(countStrikes(rig),1u);CHECK_EQ(batteryCharges(rig),2);
    CHECK_NEAR(lastStrikeDamage(rig),12,1e-9);
}
TEST(a_battery_spends_three_charges_then_reloads_full_without_auto_firing) {
    CHECK(contentLoaded());Rig rig;rig.equip(0,"battery");rig.settleEquips();const auto source=ramMob(rig);CombatSystem combat;
    for(int i=0;i<3;++i){rig.world.get<Health>(rig.player).hitCooldownUntilMillis=0;
        combat.applyDamage(rig.world,rig.player,source,1,rig.now);rig.tick();if(i<2)rig.tick(16);}
    CHECK_EQ(countStrikes(rig),3u);CHECK(rig.tickUntil([&]{return batteryCharges(rig)==3;},120));
    rig.tick(20);CHECK_EQ(countStrikes(rig),3u);
}
TEST(a_battery_remembers_one_real_hit_during_its_discharge_interval) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.equip(0, "battery", Rarity::Unique);
    for (int slot = 1; slot <= 3; ++slot) rig.equip(slot, "golden_leaf", Rarity::Unique);
    rig.world.add<PlayerSkillTree>(rig.player).skills.set(SkillId::Reload, 9);
    rig.settleEquips();
    const auto mob = ramMob(rig);
    CombatSystem combat;
    CHECK(combat.applyDamage(rig.world, rig.player, mob, 1.0, rig.now).applied > 0);
    rig.tick();
    CHECK_EQ(batteryCharges(rig), 2);
    CHECK_EQ(countStrikes(rig), 1u);
    const double ready = rig.now + kBatteryIntervalMillis;
    rig.tick(3);
    CHECK(combat.applyDamage(rig.world, rig.player, mob, 1.0, rig.now).applied > 0);
    rig.tick();
    CHECK_EQ(countStrikes(rig), 1u);
    CHECK(rig.tickUntil([&] { return countStrikes(rig) == 2; }, 20));
    CHECK_NEAR(rig.now, ready, 1e-8);
    CHECK_EQ(batteryCharges(rig), 1);
    rig.tick(14);
    CHECK_EQ(countStrikes(rig), 2u);
}

TEST(eternal_battery_reload_talent_shortens_only_the_refill_after_three_discharges) {
    const auto& registry = testsupport::shippedContent();
    const auto run = [&](bool talented) {
        Rig rig(registry);
        rig.equip(0, "battery", Rarity::Unique);
        if (talented)
            rig.world.add<PlayerSkillTree>(rig.player).skills.set(
                SkillId::Reload, skillTierCount(SkillId::Reload) - 1);
        rig.settleEquips();
        const Entity source = ramMob(rig);
        CombatSystem combat;
        double last = 0.0;
        for (int charge = 0; charge < 3; ++charge) {
            if (charge) rig.tick(9);
            CHECK(combat.applyDamage(rig.world, rig.player, source, 1, rig.now).applied > 0);
            rig.tick();
            CHECK_EQ(countStrikes(rig), static_cast<std::size_t>(charge + 1));
            if (charge) CHECK_NEAR(rig.now - last, 500.0, 1e-9);
            last = rig.now;
        }
        CHECK(rig.tickUntil([&] { return batteryCharges(rig) == 3; }, 60));
        const double refill = rig.now - last;
        CHECK_EQ(countStrikes(rig), 3u);
        return refill;
    };
    const double base = run(false);
    const double talented = run(true);
    std::printf("Eternal Battery refill: %.0f ms base, %.0f ms max Reload\n", base, talented);
    CHECK_NEAR(base, 1250.0, 50.0);
    CHECK(talented < base * .30);
}

TEST(eternal_battery_dummy_dps_depends_on_keeping_body_contact) {
    std::string error;
    const bool loaded = loadContent(testsupport::dataDir(), error);
    CHECK(loaded);
    if (!loaded) return;
    const auto& registry = content();
    const auto run = [&](bool talented, int touchEvery) {
        Rig rig(registry);
        rig.equip(0, "battery", Rarity::Unique);
        if (talented)
            rig.world.add<PlayerSkillTree>(rig.player).skills.set(
                SkillId::Reload, skillTierCount(SkillId::Reload) - 1);
        rig.settleEquips();
        NpcSystem npcs;
        const Vec2 at{1000.0, 1000.0};
        const Entity dummy = npcs.spawnNpc(rig.world, rig.terrain, registry,
            registry.mobIndex("target_dummy"), Rarity::Common, at, Realm::Overworld, rig.now);
        CHECK(dummy != NULL_ENTITY);
        if (dummy == NULL_ENTITY) return 0.0;
        const double reach = rig.world.get<Body>(dummy).radius + rig.world.get<Body>(rig.player).radius;
        CombatSystem combat;
        for (int tick = 0; tick < 200; ++tick) {
            const double gap = tick % touchEvery == 0 ? reach - 3.0 : reach + 10.0;
            rig.world.get<Transform>(rig.player).position = at - Vec2{gap, 0.0};
            rig.tick();
            auto& hp = rig.world.get<Health>(rig.player);
            hp.current = hp.max;
            ricochetCombatTick(rig, combat);
            rig.reap();
        }
        double damage = 0.0;
        for (const auto& line : combat.practiceDamage(rig.player, rig.now, dummy))
            damage += line.amount;
        return damage / 10.0;
    };
    const double held = run(false, 1);
    const double heldWithReload = run(true, 1);
    const double spaced = run(false, 30);
    const double spacedWithReload = run(true, 30);
    std::printf("Dummy DPS held: %.0f -> %.0f; spaced: %.0f -> %.0f\n",
                held, heldWithReload, spaced, spacedWithReload);
    CHECK(held > 0.0);
    CHECK(heldWithReload > held);
    CHECK(spaced > 0.0);
    CHECK_NEAR(spacedWithReload, spaced, 1e-6);
}

TEST(a_battery_ignores_self_damage_and_invulnerability) {
    CHECK(contentLoaded());Rig rig;rig.equip(0,"battery");rig.settleEquips();const auto source=ramMob(rig);CombatSystem combat;
    rig.world.get<Health>(rig.player).invulnerableUntilMillis=rig.now+1000;
    combat.applyDamage(rig.world,rig.player,source,1,rig.now);rig.tick();
    rig.world.get<Health>(rig.player).invulnerableUntilMillis=0;
    combat.applyDamage(rig.world,rig.player,rig.player,1,rig.now);rig.tick();
    CHECK_EQ(countStrikes(rig),0u);CHECK_EQ(batteryCharges(rig),3);
}

TEST(battery_retaliates_in_pvp_without_triggering_another_battery) {
    CHECK(contentLoaded());
    Rig rig;
    rig.equip(0, "battery");
    rig.settleEquips();
    const Vec2 origin = rig.position(rig.player);
    const auto makePlayer = [&](Vec2 position) {
        const Entity body = rig.world.create();
        rig.world.add<PlayerTag>(body);
        rig.world.add<Faction>(body, Faction{Team::Players, true});
        rig.world.add<Transform>(body, Transform{position, 0.0});
        rig.world.add<Body>(body, Body{10, 1});
        rig.world.add<Health>(body, Health{1000, 1000});
        return body;
    };
    const Entity attacker = makePlayer(origin + Vec2{80, 0});
    const Entity bystander = makePlayer(origin + Vec2{40, 0});
    const Entity shot = rig.world.create();
    rig.world.add<GroundEffect>(shot, GroundEffect{GroundEffectKind::Poison, attacker});
    CombatSystem combat;
    CHECK(combat.applyDamage(rig.world, rig.player, shot, 10, rig.now).applied > 0);
    rig.world.destroy(shot);
    rig.tick();
    CHECK_EQ(batteryCharges(rig), 2);
    ricochetCombatTick(rig, combat);
    const auto& attackerHealth = rig.world.get<Health>(attacker);
    CHECK(attackerHealth.current < attackerHealth.max);
    CHECK_EQ(attackerHealth.directHurtSerial, 0u);
    CHECK_NEAR(rig.world.get<Health>(bystander).fraction(), 1.0, 1e-9);
}

// ---------------------------------------------------------------------------
// The capacitor
// ---------------------------------------------------------------------------

/// PetalSystem's own capacitor figures, private to it and spelt out here for
/// the reason the battery's are.
constexpr double kCapacitorMaxCharge = 60.0;
constexpr double kCapacitorWindowMillis = 1000.0;

/// A capacitor on a frozen ring, with a mob sitting on it. Off its centre by a
/// few units, which touchesMob would otherwise read as a degenerate overlap.
Entity touchCapacitor(Rig& rig, Rarity rarity = Rarity::Common) {
    rig.equip(0, "capacitor", rarity);
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Vec2 at = rig.position(rig.petals(0).front());
    return addMob(rig, {at.x + 5.0, at.y});
}

/// Take the mob off the capacitor without taking it out of the strike's reach,
/// so the discharge still has a bolt to report.
void pullAway(Rig& rig, Entity mob) {
    const Vec2 petal = rig.position(rig.petals(0).front());
    rig.world.get<Transform>(mob).position = {petal.x + 300.0, petal.y};
}

TEST(bomb_contact_ignores_other_realms_and_dead_wild_mobs) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "bomb");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity bomb = rig.petals(0).front();
    const Vec2 at = rig.position(bomb);
    const Entity mob = addMob(rig, at + Vec2{5, 0});
    rig.world.get<Transform>(mob).realm = Realm::Maze;
    rig.tick();
    CHECK(!rig.world.has<Dead>(bomb));

    rig.world.get<Transform>(mob).realm = Realm::Overworld;
    rig.world.add<Dead>(mob);
    rig.tick();
    CHECK(!rig.world.has<Dead>(bomb));

    rig.world.remove<Dead>(mob);
    rig.tick();
    CHECK(rig.world.has<Dead>(bomb));
}

TEST(a_capacitor_discharges_what_it_banked_when_contact_ends) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "capacitor");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    // Nothing to touch: no charge, no strike.
    rig.tick(40);
    CHECK_EQ(countStrikes(rig), std::size_t(0));

    const Vec2 at = rig.position(rig.petals(0).front());
    const Entity mob = addMob(rig, {at.x + 5.0, at.y});
    // Charging, silently, for as long as the contact lasts.
    rig.tick();
    CHECK_EQ(countStrikes(rig), std::size_t(0));

    pullAway(rig, mob);
    rig.tick();
    CHECK_EQ(countStrikes(rig), std::size_t(1));
    // One point per millisecond: one tick against the mob, under the cap.
    CHECK_NEAR(lastStrikeDamage(rig), net::kTickMillis, 1e-6);

    // Spent: stepping clear again throws nothing more.
    rig.tick(40);
    CHECK_EQ(countStrikes(rig), std::size_t(1));
}

TEST(a_capacitor_holds_no_more_than_its_cap) {
    if (!contentLoaded()) return;
    Rig rig;
    const Entity mob = touchCapacitor(rig);
    // A third of a second against the mob, well past what the cap takes.
    rig.tick(10);
    CHECK_EQ(countStrikes(rig), std::size_t(0));
    pullAway(rig, mob);
    rig.tick();
    CHECK_EQ(countStrikes(rig), std::size_t(1));
    CHECK_NEAR(lastStrikeDamage(rig), kCapacitorMaxCharge, 1e-6);
}

TEST(a_capacitor_that_never_leaves_discharges_every_second) {
    if (!contentLoaded()) return;
    Rig rig;
    touchCapacitor(rig);
    const double start = rig.now;

    // Held against the mob throughout. A capacitor that only fired on leaving
    // would bank forever here.
    CHECK(rig.tickUntil([&] { return countStrikes(rig) > 0; }, 60));
    CHECK_NEAR(rig.now - start, kCapacitorWindowMillis, 1e-6);
    CHECK_NEAR(lastStrikeDamage(rig), kCapacitorMaxCharge, 1e-6);

    // And starts over rather than firing every tick from then on.
    const double first = rig.now;
    CHECK(rig.tickUntil([&] { return countStrikes(rig) > 1; }, 60));
    CHECK_NEAR(rig.now - first, kCapacitorWindowMillis, 1e-6);
}

TEST(a_capacitor_climbs_the_rarity_ladder_like_lightning) {
    if (!contentLoaded()) return;
    Rig rig;
    const Entity mob = touchCapacitor(rig, Rarity::Legendary);
    rig.tick(10);
    pullAway(rig, mob);
    rig.tick();
    CHECK_EQ(countStrikes(rig), std::size_t(1));
    CHECK_NEAR(lastStrikeDamage(rig), kCapacitorMaxCharge * petalStatScale(Rarity::Legendary),
               1e-6);
}

// ---------------------------------------------------------------------------
// Breaking and reloading
// ---------------------------------------------------------------------------

TEST(a_petal_at_zero_health_breaks_and_returns_on_its_cooldown) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    CHECK_NEAR(rig.healthOf(rig.petals(0).front()), 10.0, 1e-9);

    rig.damage(rig.petals(0).front(), 10.0);
    const double brokenAt = rig.now + net::kTickMillis;
    rig.tick();

    CHECK(rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    CHECK_NEAR(rig.slot(0).reloadReadyAtMillis, brokenAt + 1200.0, 1e-9);

    // Not a tick early: the cooldown is the whole cost of losing a petal.
    while (rig.now + net::kTickMillis < rig.slot(0).reloadReadyAtMillis) {
        rig.tick();
        CHECK(rig.slot(0).broken);
        CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    }
    const double readyAt = rig.slot(0).reloadReadyAtMillis;
    rig.tick();
    CHECK(rig.now >= readyAt);
    CHECK(rig.now < readyAt + net::kTickMillis + 1e-9);
    CHECK(!rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    // It comes back whole, not at whatever health it died with.
    CHECK_NEAR(rig.healthOf(rig.petals(0).front()), 10.0, 1e-9);
}

TEST(the_reload_talent_shortens_a_broken_petals_cooldown) {
    if (!contentLoaded()) return;

    // The same break on two flowers, one of them at the top of the Reload
    // branch. Basic's own cooldown is 1200ms, which unique cuts to 29.2%.
    const auto breakBasic = [](Rig& rig) {
        rig.equip(0, "basic");
        rig.settleEquips();
        rig.damage(rig.petals(0).front(), 10.0);
        const double brokenAt = rig.now + net::kTickMillis;
        rig.tick();
        CHECK(rig.slot(0).broken);
        return rig.slot(0).reloadReadyAtMillis - brokenAt;
    };

    Rig plain;
    CHECK_NEAR(breakBasic(plain), 1200.0, 1e-9);

    Rig unique;
    unique.world.add<PlayerSkillTree>(unique.player);
    unique.world.get<PlayerSkillTree>(unique.player)
        .skills.set(SkillId::Reload, rarityIndex(Rarity::Unique));
    CHECK_NEAR(breakBasic(unique), 350.4, 1e-9);

    // And it is a real wait, not just a shorter number on the slot. Twelve
    // ticks is 400ms: enough for unique to have its petal back, and far short
    // of the full cooldown the plain flower is still serving.
    plain.tick(12);
    unique.tick(12);
    CHECK(!unique.slot(0).broken);
    CHECK_EQ(unique.petals(0).size(), std::size_t(1));
    CHECK(plain.slot(0).broken);
}

TEST(the_reload_talent_paces_a_projectile_petals_volleys) {
    if (!contentLoaded()) return;

    // A missile petal's reload is its rate of fire: it is spent by the shot and
    // the slot pays the cooldown. Counting volleys over a fixed window is what
    // tells the two halves of that apart from a slot that merely LOOKS ready.
    const auto volleys = [](Rig& rig, int ticks) {
        rig.equip(0, "peas");
        rig.settleEquips();
        rig.setFlags(net::InputAttack);
        int fired = 0;
        bool wasBroken = rig.slot(0).broken;
        for (int i = 0; i < ticks; ++i) {
            rig.tick();
            if (rig.slot(0).broken && !wasBroken) ++fired;
            wasBroken = rig.slot(0).broken;
        }
        return fired;
    };

    Rig plain;
    const int slow = volleys(plain, 300);

    Rig unique;
    unique.world.add<PlayerSkillTree>(unique.player);
    unique.world.get<PlayerSkillTree>(unique.player)
        .skills.set(SkillId::Reload, rarityIndex(Rarity::Unique));
    const int fast = volleys(unique, 300);

    CHECK(slow > 0);
    // Not an exact ratio: the first volley of each run is paced by the equip
    // reload rather than by the shot timer, so unique lands near, not exactly
    // on, 1/0.292 times the count.
    CHECK(fast >= slow * 3);
}

TEST(a_golden_leaf_cuts_the_reload_of_the_rest_of_its_bar) {
    if (!contentLoaded()) return;
    // florr's figure, read off its tooltip builder: 2.5% a tier.
    const ContentRegistry& registry = fixture().registry;
    CHECK_NEAR(registry.petalStats(petalId("golden_leaf"), Rarity::Common).reloadCut, 0.025, 1e-9);
    CHECK_NEAR(registry.petalStats(petalId("golden_leaf"), Rarity::Ultra).reloadCut, 0.175, 1e-9);
    CHECK_NEAR(registry.petalStats(petalId("basic"), Rarity::Ultra).reloadCut, 0.0, 1e-9);

    const auto breakSlot = [](Rig& rig, int slot) {
        const auto out = rig.petals(slot);
        CHECK_EQ(out.size(), std::size_t(1));
        if (out.empty()) return -1.0;
        const Entity petal = out.front();
        rig.damage(petal, rig.healthOf(petal));
        const double brokenAt = rig.now + net::kTickMillis;
        rig.tick();
        CHECK(rig.slot(slot).broken);
        return rig.slot(slot).reloadReadyAtMillis - brokenAt;
    };

    // Basic's 1200ms, less an ultra leaf's 17.5%.
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(1, "golden_leaf", Rarity::Ultra);
    rig.settleEquips();
    CHECK_NEAR(breakSlot(rig, 0), 1200.0 * 0.825, 1e-9);

    // Its own reload too.
    const double leafReload =
        registry.petalStats(petalId("golden_leaf"), Rarity::Ultra).reloadMillis;
    CHECK_NEAR(breakSlot(rig, 1), std::max(leafReload * 0.825, net::kTickMillis), 1e-9);

    // Leaves stack, each on what the others left: an ultra and a common keep
    // 82.5% of 97.5%. And they stack with the Reload talent, multiplied.
    Rig both;
    both.world.add<PlayerSkillTree>(both.player);
    both.world.get<PlayerSkillTree>(both.player)
        .skills.set(SkillId::Reload, rarityIndex(Rarity::Unique));
    both.equip(0, "basic");
    both.equip(1, "golden_leaf", Rarity::Ultra);
    both.equip(2, "golden_leaf", Rarity::Common);
    both.settleEquips();
    CHECK_NEAR(breakSlot(both, 0), 350.4 * 0.825 * 0.975, 1e-9);
    // And a leaf's reload is cut by the other leaf as well as by itself.
    const double commonLeafReload =
        registry.petalStats(petalId("golden_leaf"), Rarity::Common).reloadMillis;
    CHECK_NEAR(breakSlot(both, 2),
               std::max(commonLeafReload * 0.292 * 0.825 * 0.975, net::kTickMillis), 1e-9);
}

TEST(partial_damage_does_not_break_a_petal) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    rig.damage(rig.petals(0).front(), 9.5);
    rig.tick(20);
    CHECK(!rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    // Damage is kept, not silently healed by the mirror that keeps a cluster
    // in step.
    CHECK_NEAR(rig.healthOf(rig.petals(0).front()), 0.5, 1e-9);
}

TEST(a_clumped_slot_gives_every_grain_its_own_health) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.settleEquips();
    for (const Entity grain : rig.petals(0)) CHECK_NEAR(rig.healthOf(grain), 12.0, 1e-9);

    // `clumped` is the second, and by far the commoner, way the reference
    // declares independent instances: `(clumped || independentHealth) &&
    // count > 1`. A four-grain clump of sand is four petals that share one
    // place on the ring, NOT one health bar shared four ways -- so a hit on
    // one grain stays on that grain.
    rig.damage(rig.petalWithSub(0, 0), 5.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 0)), 7.0, 1e-9);
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 1)), 12.0, 1e-9);

    // Two grains hit in the same tick take their own damage and nobody else's.
    rig.damage(rig.petalWithSub(0, 0), 3.0);
    rig.damage(rig.petalWithSub(0, 1), 4.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 0)), 4.0, 1e-9);
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 1)), 8.0, 1e-9);
    CHECK(!rig.slot(0).broken);

    // And one grain breaking leaves the other three on the field, which is
    // what a shared pool could never do.
    rig.damage(rig.petalWithSub(0, 0), 4.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));
    CHECK(!rig.slot(0).broken);

    CHECK(rig.tickUntil([&] { return rig.petals(0).size() == 4; }));
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 0)), 12.0, 1e-9);
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 1)), 8.0, 1e-9);
}

TEST(independent_health_petals_break_one_at_a_time) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "shards");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));

    // Damage on one shard stays on that shard.
    rig.damage(rig.petalWithSub(0, 0), 4.0);
    rig.tick();
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 0)), 2.0, 1e-9);
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 1)), 6.0, 1e-9);

    rig.damage(rig.petalWithSub(0, 1), 6.0);
    const double brokenAt = rig.now + net::kTickMillis;
    rig.tick();
    // One down, two still out: the slot is not on cooldown.
    CHECK_EQ(rig.petals(0).size(), std::size_t(2));
    CHECK(!rig.slot(0).broken);
    CHECK(rig.petalWithSub(0, 1) == NULL_ENTITY);
    const PetalSlotState& state = rig.world.get<PetalSlotState>(rig.player);
    CHECK_NEAR(state.slots[0].instanceReadyAtMillis[1], brokenAt + 500.0, 1e-9);

    CHECK(rig.tickUntil([&] { return rig.petals(0).size() == 3; }));
    CHECK(!rig.slot(0).broken);
    // The one that came back is whole; the one that was only grazed is not
    // healed by its neighbour's return.
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 1)), 6.0, 1e-9);
    CHECK_NEAR(rig.healthOf(rig.petalWithSub(0, 0)), 2.0, 1e-9);
}

TEST(a_combat_killed_independent_petal_still_pays_its_reload) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "shards");
    rig.settleEquips();

    // As the server does it: combat marks the kill, and the reaper destroys
    // the petal at the end of that same tick. The ring pass never sees the
    // corpse -- only a grain that is missing.
    const Entity shard = rig.petalWithSub(0, 1);
    const double brokenAt = rig.now + net::kTickMillis;
    rig.world.add<Dead>(shard);
    rig.reap();
    rig.tick();

    CHECK_EQ(rig.petals(0).size(), std::size_t(2));
    const PetalSlotState& state = rig.world.get<PetalSlotState>(rig.player);
    CHECK_NEAR(state.slots[0].instanceReadyAtMillis[1], brokenAt + feedbackNaturalReload("shards"), 1e-9);
    // Stays down for the whole reload, as a single petal would, and not a
    // tick longer.
    while (rig.now + net::kTickMillis < brokenAt + feedbackNaturalReload("shards")) {
        rig.tick();
        CHECK_EQ(rig.petals(0).size(), std::size_t(2));
    }
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));
}

TEST(a_clumped_grain_killed_and_reaped_pays_the_same_reload_as_a_single_petal) {
    if (!contentLoaded()) return;
    // The two health models side by side, both killed the way combat kills:
    // marked Dead, reaped before the ring pass. A clump must not reload any
    // faster than a single petal does.
    Rig single;
    single.equip(0, "basic");
    single.settleEquips();
    single.world.add<Dead>(single.petals(0).front());
    single.reap();
    const double singleBrokenAt = single.now + net::kTickMillis;
    single.tick();
    CHECK(single.slot(0).broken);
    CHECK_NEAR(single.slot(0).reloadReadyAtMillis, singleBrokenAt + feedbackNaturalReload("basic"), 1e-9);

    Rig clump;
    clump.equip(0, "sandy");
    clump.settleEquips();
    CHECK_EQ(clump.petals(0).size(), std::size_t(4));
    for (const Entity grain : clump.petals(0)) clump.world.add<Dead>(grain);
    clump.reap();
    const double clumpBrokenAt = clump.now + net::kTickMillis;
    clump.tick();
    CHECK_EQ(clump.petals(0).size(), std::size_t(0));
    CHECK(clump.slot(0).broken);
    CHECK_NEAR(clump.slot(0).reloadReadyAtMillis, clumpBrokenAt + feedbackNaturalReload("sandy"), 1e-9);
    clump.tick(5);
    CHECK_EQ(clump.petals(0).size(), std::size_t(0));
    CHECK(clump.tickUntil([&] { return clump.petals(0).size() == 4; }));
    CHECK(clump.now >= clumpBrokenAt + feedbackNaturalReload("sandy"));
}

TEST(an_independent_slot_reads_as_broken_only_when_all_of_it_is_down) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "shards");
    rig.settleEquips();
    for (const Entity shard : rig.petals(0)) rig.damage(shard, 6.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    CHECK(rig.slot(0).broken);
    CHECK(rig.slot(0).reloadReadyAtMillis > rig.now);

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));
}

TEST(a_petal_with_no_health_pool_can_never_break) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "rock");
    rig.settleEquips();
    const std::vector<Entity> petals = rig.petals(0);
    CHECK_EQ(petals.size(), std::size_t(1));
    // No Health component at all: an unbreakable petal is not one with zero
    // hit points, which would break on its first tick.
    CHECK(!rig.world.has<Health>(petals.front()));

    rig.tick(200);
    CHECK(!rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
}

TEST(a_broken_petal_leaves_its_gap_open) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    CHECK_EQ(rig.petals().size(), std::size_t(3));   // two basics and the anchor

    rig.damage(rig.petals(0).front(), 10.0);
    rig.tick();
    CHECK_EQ(rig.petals().size(), std::size_t(2));

    // The survivor keeps the third of the ring it already had. Re-spacing the
    // remaining petals would make every break a visible lurch.
    const Entity survivor = rig.petals(1).front();
    rig.settleRing();
    CHECK_NEAR(angularGap(rig.angleOf(survivor), rig.ring().spin + kTau / 3.0), 0.0, 1e-6);
}

TEST(swapping_a_petal_rebuilds_the_slot_from_scratch) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    rig.damage(rig.petals(0).front(), 8.0);
    rig.tick();
    CHECK_NEAR(rig.healthOf(rig.petals(0).front()), 2.0, 1e-9);

    rig.equip(0, "sandy");
    rig.tick();
    CHECK(rig.petals(0).empty());
    CHECK(rig.slot(0).broken);

    rig.settleEquips();
    const std::vector<Entity> grains = rig.petals(0);
    CHECK_EQ(grains.size(), std::size_t(4));
    for (const Entity grain : grains) CHECK_NEAR(rig.healthOf(grain), 12.0, 1e-9);
    CHECK(!rig.slot(0).broken);
}

TEST(re_equipping_over_a_broken_slot_serves_the_new_petals_reload) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    rig.damage(rig.petals(0).front(), 10.0);
    rig.tick();
    CHECK(rig.slot(0).broken);

    // Not the old petal's remaining cooldown, and not none: the shards' own
    // reload, from the tick the slot saw the edit.
    rig.equip(0, "shards");
    const double equippedAt = rig.now + net::kTickMillis;
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.slot(0).reloadReadyAtMillis, equippedAt + feedbackEquipReload("shards"), 1e-9);
    CHECK(rig.petals(0).empty());

    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));
}

TEST(swapping_two_slots_reloads_both_clump_and_single_alike) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.equip(1, "basic");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));

    // As the server applies a bar swap: only the contents trade places. The
    // ring's own bookkeeping (`broken`, the ready time) is left for the pass.
    LoadoutSlot& a = rig.world.get<Loadout>(rig.player).slots[0];
    LoadoutSlot& b = rig.world.get<Loadout>(rig.player).slots[1];
    std::swap(a.configIndex, b.configIndex);
    std::swap(a.rarity, b.rarity);
    const double swappedAt = rig.now + net::kTickMillis;
    rig.tick();

    // Nothing comes straight back, the clump included: every grain owes the
    // full reload, as the single petal does.
    CHECK(rig.petals(0).empty());
    CHECK(rig.petals(1).empty());
    CHECK(rig.slot(0).broken);
    CHECK(rig.slot(1).broken);
    CHECK_NEAR(rig.slot(0).reloadReadyAtMillis, swappedAt + feedbackEquipReload("basic"), 1e-9);
    CHECK_NEAR(rig.slot(1).reloadReadyAtMillis, swappedAt + feedbackEquipReload("sandy"), 1e-9);
    for (const double ready : rig.world.get<PetalSlotState>(rig.player).slots[1].instanceReadyAtMillis) {
        CHECK_NEAR(ready, swappedAt + feedbackEquipReload("sandy"), 1e-9);
    }

    // And the sand is back once that reload is served, not a tick sooner.
    while (rig.now + net::kTickMillis < swappedAt + feedbackEquipReload("sandy")) {
        rig.tick();
        CHECK(rig.petals(1).empty());
    }
    CHECK(rig.tickUntil([&] { return rig.petals(1).size() == 4; }, 3));
    CHECK(!rig.slot(1).broken);
}

TEST(unequipping_removes_the_petals_immediately) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.settleEquips();
    CHECK_EQ(rig.petals().size(), std::size_t(4));
    rig.unequip(0);
    rig.tick();
    CHECK_EQ(rig.petals().size(), std::size_t(0));
    CHECK(rig.world.get<Loadout>(rig.player).spawned.empty());
}

TEST(death_clears_the_ring_and_respawning_rebuilds_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(1, "sandy");
    rig.settleEquips();
    CHECK_EQ(rig.petals().size(), std::size_t(5));

    rig.world.get<Health>(rig.player).current = 0.0;
    rig.tick();
    CHECK_EQ(rig.petals().size(), std::size_t(0));
    CHECK(rig.world.get<Loadout>(rig.player).spawned.empty());
    // The reload debt dies with the body: coming back mid-cooldown on every
    // slot would be a harsher punishment than losing the petals was.
    CHECK(!rig.slot(0).broken);

    rig.world.get<Health>(rig.player).current = 100.0;
    rig.tick();
    CHECK_EQ(rig.petals().size(), std::size_t(5));
}

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

TEST(modifiers_are_summed_from_scratch_and_vanish_when_unequipped) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "lucky");
    rig.tick();
    CHECK_NEAR(rig.modifiers().luck, 3.0, 1e-9);
    CHECK_NEAR(rig.modifiers().speedScale, 1.5, 1e-9);
    CHECK_NEAR(rig.modifiers().magnetism, 50.0, 1e-9);

    // A hundred ticks must not accumulate a hundred bonuses.
    rig.tick(100);
    CHECK_NEAR(rig.modifiers().luck, 3.0, 1e-9);
    CHECK_NEAR(rig.modifiers().speedScale, 1.5, 1e-9);

    rig.equip(1, "lucky");
    rig.tick();
    CHECK_NEAR(rig.modifiers().luck, 5.0, 1e-9);
    CHECK_NEAR(rig.modifiers().speedScale, 1.5 * 1.5, 1e-9);
    CHECK_NEAR(rig.modifiers().magnetism, 100.0, 1e-9);

    rig.unequip(0);
    rig.unequip(1);
    rig.tick();
    CHECK_NEAR(rig.modifiers().luck, 1.0, 1e-12);
    CHECK_NEAR(rig.modifiers().speedScale, 1.0, 1e-12);
    CHECK_NEAR(rig.modifiers().magnetism, kBaseMagnetism, 1e-12);
}

TEST(magnetism_talent_adds_reach_on_top_of_worn_magnetism) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.world.ensure<PlayerSkillTree>(rig.player);
    rig.equip(0, "lucky");
    // Looked up each time, not held: equipping moves the player's components.
    const auto skills = [&]() -> SkillSet& { return rig.world.get<PlayerSkillTree>(rig.player).skills; };
    skills().set(SkillId::Loadout, 4);
    for (int tier = 0; tier < skillTierCount(SkillId::Magnetism); ++tier) {
        skills().set(SkillId::Magnetism, tier);
        rig.tick();
        CHECK_NEAR(rig.modifiers().magnetism,
                   1050.0, 1e-9);
    }

    // Held without the fifth Loadout upgrade, the branch grants nothing.
    skills().set(SkillId::Loadout, 3);
    rig.tick();
    CHECK_NEAR(rig.modifiers().magnetism, 50.0, 1e-9);
}

TEST(worn_evasion_grows_a_step_a_tier_and_copies_are_independent_rolls) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.tick();
    CHECK_NEAR(rig.modifiers().evasion, 0.0, 1e-12);

    rig.equip(0, "charm");
    rig.tick();
    CHECK_NEAR(rig.modifiers().evasion, 0.1, 1e-12);

    // Rare is the third tier: three steps of the authored 0.1.
    rig.equip(0, "charm", Rarity::Rare);
    rig.tick();
    CHECK_NEAR(rig.modifiers().evasion, 0.3, 1e-12);

    // Two of them do not make 60%: a hit has to get past both rolls.
    rig.equip(1, "charm", Rarity::Rare);
    rig.tick();
    CHECK_NEAR(rig.modifiers().evasion, 1.0 - 0.7 * 0.7, 1e-12);

    rig.unequip(0);
    rig.unequip(1);
    rig.tick();
    CHECK_NEAR(rig.modifiers().evasion, 0.0, 1e-12);
}

TEST(the_strongest_worn_aggro_range_cut_wins_and_copies_do_not_multiply) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.tick();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.0, 1e-12);

    rig.equip(0, "stinky");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 0.75, 1e-12);

    // A second common is still -25%, not the -43.75% that is an uncommon's;
    // a better tier beside it takes over.
    rig.equip(1, "stinky");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 0.75, 1e-12);
    rig.equip(2, "stinky", Rarity::Epic);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, std::pow(0.75, 4), 1e-12);

    rig.unequip(0);
    rig.unequip(1);
    rig.unequip(2);
    rig.tick();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.0, 1e-12);
}

TEST(the_strongest_worn_aggro_range_boost_wins_and_multiplies_the_strongest_cut) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "bright");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.33, 1e-12);

    // Boosts are held to the cut's rule from the other side: a second common
    // is still 1.33, and a better tier takes over rather than stacking.
    rig.equip(1, "bright");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.33, 1e-12);
    rig.equip(2, "bright", Rarity::Rare);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, std::pow(1.33, 3), 1e-12);

    // A poo beside them neither loses to the boost nor hides it: one cut and
    // one boost, multiplied.
    rig.equip(3, "stinky");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, std::pow(1.33, 3) * 0.75, 1e-12);

    rig.unequip(0);
    rig.unequip(1);
    rig.unequip(2);
    rig.tick();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 0.75, 1e-12);
}

TEST(bulb_extra_slots_stack_a_small_share_and_stop_at_the_base_range_cap) {
    const auto& content = testsupport::shippedContent();
    Rig rig(content);
    const auto bulb = content.petalIndex("bulb");
    const auto scale = [&](Rarity rarity) { return content.petalStats(bulb, rarity).modifiers.aggroRange; };
    rig.equip(0, "bulb");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, scale(Rarity::Common), 1e-9);
    rig.equip(1, "bulb");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, scale(Rarity::Common) + 0.066, 1e-9);

    rig.equip(2, "bulb", Rarity::Apex);
    rig.equip(3, "bulb", Rarity::Apex);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, scale(Rarity::Apex) + 0.30, 1e-9);
    rig.damage(rig.petals(3).front(), 1e9);
    rig.tick();
    CHECK(rig.slot(3).broken);
    CHECK_NEAR(rig.modifiers().aggroRangeScale, scale(Rarity::Apex) + 0.132, 1e-9);
}

TEST(aggro_modifiers_work_only_while_the_petal_is_on_the_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "stinky");
    rig.equip(1, "glowy");

    // Equipped but still serving the equip reload: nothing yet.
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK(rig.slot(1).broken);
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.0, 1e-12);
    CHECK_NEAR(rig.modifiers().aggroRadiusBonus, 0.0, 1e-12);

    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 0.75, 1e-12);
    CHECK_NEAR(rig.modifiers().aggroRadiusBonus, 150.0, 1e-9);

    // Each one lapses on its own when it breaks, and only for its reload.
    rig.damage(rig.petals(0).front(), 5.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 1.0, 1e-12);
    CHECK_NEAR(rig.modifiers().aggroRadiusBonus, 150.0, 1e-9);

    rig.damage(rig.petals(1).front(), 10.0);
    rig.tick();
    CHECK(rig.slot(1).broken);
    CHECK_NEAR(rig.modifiers().aggroRadiusBonus, 0.0, 1e-12);

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken && !rig.slot(1).broken; }));
    CHECK_NEAR(rig.modifiers().aggroRangeScale, 0.75, 1e-12);
    CHECK_NEAR(rig.modifiers().aggroRadiusBonus, 150.0, 1e-9);
}

TEST(a_broken_petal_keeps_its_equipment_modifier_while_reloading) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "lucky");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().luck, 3.0, 1e-9);

    rig.damage(rig.petals(0).front(), 5.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.modifiers().luck, 3.0, 1e-12);
    CHECK_NEAR(rig.modifiers().speedScale, 1.5, 1e-12);

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    CHECK_NEAR(rig.modifiers().luck, 3.0, 1e-9);
}

TEST(a_sponge_defers_damage_only_while_its_body_is_alive) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sponge", Rarity::Rare);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().spongeDamageDurationMillis, 2000.0, 1e-9);

    rig.damage(rig.petals(0).front(), 10.0 * petalStatScale(Rarity::Rare));
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.modifiers().spongeDamageDurationMillis, 0.0, 1e-12);
}

TEST(salt_reflects_only_while_out_and_a_second_one_never_adds) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "briny");

    // Equipped but still serving the equip reload: gardn only reflects for a
    // salt that has actually spawned.
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.modifiers().damageReflection, 0.0, 1e-12);

    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().damageReflection, 0.25, 1e-12);
    CHECK(rig.modifiers().damageReflectionRarity == Rarity::Common);

    // "Does not stack with itself": the better salt wins, the two never sum.
    rig.equip(1, "brinier");
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().damageReflection, 0.4, 1e-12);

    // Better means against the same attacker, so the tier counts: a rare
    // briny repays nine times its 0.25 from a common mob, which beats 0.4.
    rig.equip(1, "briny", Rarity::Rare);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().damageReflection, 0.25, 1e-12);
    CHECK(rig.modifiers().damageReflectionRarity == Rarity::Rare);

    rig.unequip(1);
    rig.damage(rig.petals(0).front(), 5.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.modifiers().damageReflection, 0.0, 1e-12);

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    CHECK_NEAR(rig.modifiers().damageReflection, 0.25, 1e-12);
    CHECK(rig.modifiers().damageReflectionRarity == Rarity::Common);
}

TEST(a_yucca_heals_only_while_the_flower_is_blocking) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "yuccaish");
    rig.settleEquips();
    const double yucca =
        fixture().registry.petalStats(petalId("yuccaish"), Rarity::Common).passiveHealPerSecond;
    CHECK(yucca > 0.0);

    // Worn but not blocking: the base regeneration and nothing more.
    rig.tick();
    CHECK_NEAR(rig.modifiers().passiveHealPerSecond, 1.0, 1e-12);

    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK_NEAR(rig.modifiers().passiveHealPerSecond, 1.0 + yucca, 1e-12);

    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK_NEAR(rig.modifiers().passiveHealPerSecond, 1.0, 1e-12);

    // Both keys is a lunge, not a block (see the ring test above), so the
    // ring is out and the yucca pays nothing.
    rig.setFlags(net::InputAttack | net::InputDefend);
    rig.tick();
    CHECK_NEAR(rig.modifiers().passiveHealPerSecond, 1.0, 1e-12);

    // The health itself follows the stance, not just the published rate.
    Health& health = rig.world.get<Health>(rig.player);
    health.current = health.max * 0.5;
    const double before = health.current;
    rig.setFlags(0);
    rig.tick(30);
    const double idleGain = health.current - before;
    const double mid = health.current;
    rig.setFlags(net::InputDefend);
    rig.tick(30);
    const double blockGain = health.current - mid;
    CHECK(blockGain > idleGain);
}

TEST(a_leaf_heals_whatever_the_stance) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "leafy");
    rig.settleEquips();
    const double leaf =
        fixture().registry.petalStats(petalId("leafy"), Rarity::Common).passiveHealPerSecond;
    for (const std::uint8_t flags :
         {std::uint8_t{0}, std::uint8_t{net::InputDefend}, std::uint8_t{net::InputAttack},
          std::uint8_t{net::InputAttack | net::InputDefend}}) {
        rig.setFlags(flags);
        auto& health = rig.world.get<Health>(rig.player);
        const double before = health.max * 0.5;
        health.current = before;
        rig.tick();
        CHECK_NEAR(rig.modifiers().passiveHealPerSecond, 1.0 + leaf, 1e-12);
        CHECK_NEAR(rig.world.get<Health>(rig.player).current - before,
                   (1.0 + leaf) * net::kTickSeconds * kPlayerHealMultiplier, 1e-9);
    }
}

TEST(a_sponge_publishes_the_damage_it_is_holding_for_the_bar) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sponge");
    rig.equip(1, "basic");
    rig.settleEquips();

    // Two deferred hits: the bar prints what the FLOWER is still holding, not
    // a share per hit -- combat defers a hit against the player.
    SpongeDamageState& stored = rig.world.ensure<SpongeDamageState>(rig.player);
    stored.effects.push_back(SpongeDamageEffect{9.0, 9.0, NULL_ENTITY});
    stored.effects.push_back(SpongeDamageEffect{5.0, 5.0, NULL_ENTITY});
    rig.tick();
    CHECK_NEAR(rig.slotCounter(0), 14.0, 1e-9);
    // A petal with no number of its own says so, rather than printing a zero.
    CHECK(rig.slotCounter(1) < 0.0);

    // Paid back: the number goes with the damage.
    rig.world.get<SpongeDamageState>(rig.player).effects.clear();
    rig.tick();
    CHECK_NEAR(rig.slotCounter(0), 0.0, 1e-9);

    // And a slot that stops being a sponge stops having a number at all, even
    // while the flower is still paying the old hits back.
    rig.world.get<SpongeDamageState>(rig.player)
        .effects.push_back(SpongeDamageEffect{4.0, 4.0, NULL_ENTITY});
    rig.equip(0, "basic");
    rig.tick();
    CHECK(rig.slotCounter(0) < 0.0);
}

TEST(a_broken_sponge_goes_on_printing_what_it_absorbed) {
    // The stored hits drain whether or not the body that took them survived,
    // so the number stays up while the slot reloads. A number that vanished at
    // the moment it mattered most would read as the damage being cancelled.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sponge");
    rig.settleEquips();

    rig.world.ensure<SpongeDamageState>(rig.player)
        .effects.push_back(SpongeDamageEffect{11.0, 11.0, NULL_ENTITY});
    rig.damage(rig.petals(0).front(), 10.0);
    rig.tick();

    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.slotCounter(0), 11.0, 1e-9);
}

TEST(a_downed_flower_prints_no_stored_damage_on_its_death_card) {
    // reconcileSlots steps over a corpse, so a number left standing would
    // freeze there for as long as the death card is up -- which is how this
    // read as a feature that only worked when you were dead. The ring pass
    // drops it with the ring.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sponge");
    rig.settleEquips();

    rig.world.ensure<SpongeDamageState>(rig.player)
        .effects.push_back(SpongeDamageEffect{18.0, 0.0, NULL_ENTITY});
    rig.tick();
    CHECK_NEAR(rig.slotCounter(0), 18.0, 1e-9);

    rig.world.add<Dead>(rig.player);
    rig.tick();
    CHECK(rig.slotCounter(0) < 0.0);

    // And a revive republishes it: the corpse kept what it was holding.
    rig.world.remove<Dead>(rig.player);
    rig.tick();
    CHECK_NEAR(rig.slotCounter(0), 18.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Root: banking the armour combat spends
// ---------------------------------------------------------------------------

namespace {
/// The stacks a flower is holding, and 0 for one that has never had a root on.
int bankedStacks(Rig& rig) {
    const ArmorStackState* state = rig.world.tryGet<ArmorStackState>(rig.player);
    return state != nullptr ? state->stacks : 0;
}

/// Ticks between two stacks. ROUNDED, not truncated: a tick is 33.333... ms
/// and sixty of them sum to a hair under two seconds, so the honest answer is
/// sixty and the truncating one is fifty-nine.
const int kTicksPerStack =
    static_cast<int>(std::lround(kArmorStackIntervalMillis / net::kTickMillis));
}  // namespace

TEST(a_root_banks_one_stack_every_two_seconds_and_stops_at_ten) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.tick();

    // Nothing at first: the bank starts empty and the interval has to pass
    // before it pays out. A stack on the equipping tick would hand a flower
    // one for free every time it swapped the petal in.
    CHECK_EQ(bankedStacks(rig), 0);

    rig.tick(kTicksPerStack);
    CHECK_EQ(bankedStacks(rig), 1);
    rig.tick(kTicksPerStack);
    CHECK_EQ(bankedStacks(rig), 2);

    // Capped. Twenty seconds fills it; another twenty adds nothing.
    rig.tick(kTicksPerStack * (kMaxArmorStacks + 4));
    CHECK_EQ(bankedStacks(rig), kMaxArmorStacks);

    // And the petal publishes what a stack is worth, which is what combat
    // subtracts. Common tier: the authored figure, unscaled.
    CHECK_NEAR(rig.world.get<ArmorStackState>(rig.player).perStack, 12.0, 1e-9);
    CHECK_NEAR(rig.modifiers().armorPerStack, 12.0, 1e-9);
}

TEST(a_stack_spent_at_the_cap_comes_back_on_the_timers_own_cadence) {
    // gardn cycles the counter whether or not a stack is owed, so a flower
    // that spends one while full is not made to wait a fresh full interval.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.tick(kTicksPerStack * (kMaxArmorStacks + 1));
    CHECK_EQ(bankedStacks(rig), kMaxArmorStacks);

    // Spend one just before the next crossing, as combat would.
    rig.tick(kTicksPerStack - 2);
    --rig.world.get<ArmorStackState>(rig.player).stacks;
    rig.tick(2);
    CHECK_EQ(bankedStacks(rig), kMaxArmorStacks);
}

TEST(taking_the_root_off_empties_the_bank) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.tick(kTicksPerStack * 3);
    CHECK_EQ(bankedStacks(rig), 3);

    // Banked stacks are not a possession. Carrying them over would let a
    // player bank ten and then fight on a loadout with no root in it.
    rig.unequip(0);
    rig.tick();
    CHECK_EQ(bankedStacks(rig), 0);
    CHECK_NEAR(rig.world.get<ArmorStackState>(rig.player).perStack, 0.0, 1e-12);
}

TEST(a_broken_root_goes_on_banking) {
    // Unlike a sponge, which has to be out to catch a hit: root banks on a
    // timer the FLOWER runs, and a petal that stopped earning the moment it
    // broke would stop exactly while the flower is being hit.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.settleEquips();
    rig.damage(rig.petals(0).front(), 100.0);
    rig.tick();
    CHECK(rig.slot(0).broken);

    const int before = bankedStacks(rig);
    rig.tick(kTicksPerStack);
    CHECK_EQ(bankedStacks(rig), before + 1);
}

TEST(a_downed_flower_loses_the_armour_it_banked) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.tick(kTicksPerStack * 4);
    CHECK_EQ(bankedStacks(rig), 4);

    rig.world.add<Dead>(rig.player);
    rig.tick();
    CHECK_EQ(bankedStacks(rig), 0);

    // A revive starts the bank over rather than handing back what the corpse
    // was holding.
    rig.world.remove<Dead>(rig.player);
    rig.tick();
    CHECK_EQ(bankedStacks(rig), 0);
}

TEST(two_roots_are_one_bank_at_the_better_petals_strength) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.equip(1, "root", Rarity::Rare);
    rig.tick(kTicksPerStack);

    // One stack for the pair, not one each -- and worth the rare petal's
    // figure, which is the common one up two tiers of the 3x ladder.
    CHECK_EQ(bankedStacks(rig), 1);
    CHECK_NEAR(rig.modifiers().armorPerStack, 12.0 * 9.0, 1e-9);
}

TEST(a_root_prints_the_stacks_it_is_holding_on_the_bar) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "root");
    rig.equip(1, "basic");
    rig.settleEquips();

    // A root always prints, zero included: the gauge is the petal saying how
    // much it has ready, and one that disappeared while empty could not be
    // told from a petal that never had one.
    CHECK_NEAR(rig.slotCounter(0), static_cast<double>(bankedStacks(rig)), 1e-9);
    CHECK(rig.slotCounter(1) < 0.0);

    rig.tick(kTicksPerStack * 2);
    const double printed = rig.slotCounter(0);
    CHECK_NEAR(printed, static_cast<double>(bankedStacks(rig)), 1e-9);
    CHECK(printed >= 2.0);

    // And a slot that stops being a root stops having a number at all.
    rig.equip(0, "basic");
    rig.tick();
    CHECK(rig.slotCounter(0) < 0.0);
}

TEST(a_clump_pays_its_modifier_once_not_once_per_grain) {
    if (!contentLoaded()) return;
    Rig rig;
    // sandy carries no modifiers, so this checks the slot-vs-instance rule
    // through the one number a four-count petal would inflate: the count of
    // contributions, seen here as the reach staying the reacher's own.
    rig.equip(0, "sandy");
    rig.equip(1, "reacher");
    rig.settleEquips();
    CHECK_EQ(rig.petals().size(), std::size_t(5));
    CHECK_NEAR(rig.modifiers().attackRange, 50.0, 1e-9);
}

TEST(rarity_scales_a_petals_published_numbers) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "toxic", Rarity::Rare);
    rig.settleEquips();
    const Entity petal = rig.petals(0).front();

    // Rare is two tiers up: a flat 3x per tier on damage and poison.
    const double scale = petalStatScale(Rarity::Rare);
    CHECK_NEAR(rig.world.get<ContactDamage>(petal).amount, 2.0 * scale, 1e-9);
    // The riders are not published on the petal: combat reads them off the
    // content by the petal's own index and tier (gatherPetals), so that is
    // where they are checked.
    const PetalInstance& instance = rig.world.get<PetalInstance>(petal);
    const PetalStats effect = fixture().registry.petalStats(instance.configIndex, instance.rarity);
    CHECK_NEAR(effect.poisonPerSecond, 50.0 * scale, 1e-6);
    // Duration is flat: rarity buys damage, not a longer debuff.
    CHECK_NEAR(effect.poisonDurationMillis, 3000.0, 1e-9);
    CHECK_NEAR(rig.healthOf(petal), 5.0 * scale, 1e-9);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

TEST(a_burst_heal_charges_homes_consumes_and_reloads) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "healer");
    rig.settleEquips();
    constexpr double startHealth = 1.0;
    rig.world.get<Health>(rig.player).current = startHealth;
    rig.setFlags(net::InputAttack);
    const double startedAt = rig.now;

    // Check at delivery so a short reload cannot finish before the assertion.
    CHECK(rig.tickUntil([&] { return rig.slot(0).broken; }, 200));
    const auto& health = rig.world.get<Health>(rig.player);
    const double expected = startHealth +
        (10.0 + (rig.now - startedAt) / 1000.0) * kPlayerHealMultiplier;
    CHECK_NEAR(health.current, std::min(expected, health.max), 1e-9);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    rig.world.get<Health>(rig.player).current = startHealth;
    CHECK(rig.tickUntil([&] { return rig.slot(0).broken; }, 200));
}

/// The same burst, on a flower running flat out.
///
/// A homing petal closes on the flower with a first-order glide, and a glide
/// toward a MOVING target settles at a standing lag rather than arriving: at
/// top speed that gap is some 28 units, wider than the flower's own 20-unit
/// body, so the rose trails it and is never touched. It is the dive's landing
/// window that delivers the burst here, not contact -- without one the petal
/// chases a sprinting player forever, healing nothing and never reloading.
TEST(a_burst_heal_lands_on_a_flower_running_at_top_speed) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "healer");
    rig.settleEquips();
    rig.world.get<Health>(rig.player).current = 50.0;

    // Movement is off in this rig unless withMovement() turns it on, and this
    // test leaves it off, so the sprint is applied by hand: one tick of top
    // speed, written where movement would have written it.
    const Vec2 stride{kPlayerMaxSpeed * net::kTickSeconds, 0.0};
    bool delivered = false;
    for (int i = 0; i < 200 && !delivered; ++i) {
        rig.world.get<Transform>(rig.player).position += stride;
        rig.tick();
        delivered = rig.slot(0).broken;
    }

    CHECK(delivered);
    CHECK(rig.world.get<Health>(rig.player).current >= 60.0);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
}

/// And the dive that never lands pays out regardless.
///
/// Contact is how a burst is normally spent, but it is not guaranteed -- a
/// wall the petal is pushed back out of can hold it off the flower for as long
/// as the flower stands behind it. Stranded mid-dive the petal heals nothing
/// and never dies, so the slot never reloads and the rose simply stops being a
/// petal. Here the gap is held open by hand, which is that case in the small.
TEST(a_burst_heal_that_cannot_reach_the_flower_is_absorbed_anyway) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "healer");
    rig.settleEquips();
    rig.world.get<Health>(rig.player).current = 50.0;

    bool delivered = false;
    int ticks = 0;
    for (; ticks < 200 && !delivered; ++ticks) {
        // Whatever the dive managed last tick, drag the petal back out of
        // reach: the gap never closes, however long it chases.
        const std::vector<Entity> out = rig.petals(0);
        if (!out.empty()) {
            rig.world.get<Transform>(out.front()).position =
                rig.position(rig.player) + Vec2{400.0, 0.0};
        }
        rig.tick();
        delivered = rig.slot(0).broken;
    }

    CHECK(delivered);
    CHECK(rig.world.get<Health>(rig.player).current >= 60.0);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    // Waited out rather than paid early: the burst is only given up on once
    // the dive has had its window, which is well past the third of a second a
    // landing dive takes.
    CHECK(ticks * net::kTickMillis >= kPetalHomingTimeoutMillis);
}

TEST(a_dandelions_lockout_stops_the_ring_healing_at_all) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "healer");
    rig.settleEquips();
    rig.world.get<Health>(rig.player).current = 50.0;
    // Well past the burst's own charge and past a second of passive
    // regeneration, so a lockout that leaked anywhere would show.
    rig.world.ensure<Afflictions>(rig.player).noHealUntilMillis = rig.now + 10000.0;
    rig.setFlags(net::InputAttack);
    rig.tick(60);

    CHECK_NEAR(rig.world.get<Health>(rig.player).current, 50.0, 1e-9);
    // The rose is still in orbit: gardn refuses the CHARGE, so the petal is
    // not spent on a heal that would land nothing and is still there when the
    // lockout lapses.
    CHECK(!rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));

    // And once it lapses, both come back.
    rig.world.get<Afflictions>(rig.player).noHealUntilMillis = 0.0;
    CHECK(rig.tickUntil([&] { return rig.world.get<Health>(rig.player).current > 55.0; }, 200));
}

TEST(a_shell_homes_grants_one_temporary_shield_and_is_consumed) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "shell");
    rig.settleEquips();
    rig.tick(static_cast<int>(std::ceil((40.0 / 30.0) / net::kTickSeconds)));

    const ShieldState& shield = rig.world.get<ShieldState>(rig.player);
    CHECK_NEAR(shield.amount, 22.0, 1e-9);
    CHECK(shield.active());
    CHECK(rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
}

TEST(a_shell_stacks_its_shield_like_a_rose_heals_up_to_max_health) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "shell");
    rig.settleEquips();
    rig.tick(40);
    ShieldState* shield = &rig.world.get<ShieldState>(rig.player);
    // Delivered a few ticks back, and shrinking since.
    CHECK(shield->amount > 21.0 && shield->amount <= 22.0);

    // Still up and below max health: the reloaded shell flies home again and
    // ADDS its figure, as a rose adds to health.
    double before = shield->amount;
    double grownFrom = 0.0;
    CHECK(rig.tickUntil([&] {
        const double now = rig.world.get<ShieldState>(rig.player).amount;
        if (now > before) {
            grownFrom = before;
            return true;
        }
        before = now;
        return false;
    }, 300));
    shield = &rig.world.get<ShieldState>(rig.player);
    CHECK(grownFrom > 0.0);
    CHECK(shield->amount > grownFrom + 21.0 && shield->amount <= grownFrom + 22.0);

    // ...but never past the flower's max health. Held just under it until the
    // next shell lands.
    const double max = rig.world.get<Health>(rig.player).max;
    CHECK(rig.tickUntil([&] {
        ShieldState& s = rig.world.get<ShieldState>(rig.player);
        if (s.amount > max - 5.0) return true;
        s.amount = max - 5.0;
        return false;
    }, 300));
    CHECK_NEAR(rig.world.get<ShieldState>(rig.player).amount, max, 1e-9);
}

// ---------------------------------------------------------------------------
// Mana
// ---------------------------------------------------------------------------

TEST(a_bar_with_no_magic_petal_has_no_mana_pool_at_all) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    rig.tick(20);
    // Not a pool reading zero -- no pool. Every flower in the world would
    // otherwise carry a component that only magic builds ever look at.
    CHECK(rig.world.tryGet<ManaPool>(rig.player) == nullptr);
}

TEST(a_worn_vessel_grants_a_partial_pool_and_doubles_capacity_per_tier) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.settleEquips();
    rig.tick();
    const ManaPool& pool = rig.world.get<ManaPool>(rig.player);
    CHECK_NEAR(pool.max, 100.0, 1e-9);
    CHECK_NEAR(pool.current, 40.0, 1e-9);

    // Two vessels are two grants: the pool is summed over the bar, unlike the
    // lotus threshold beside it.
    rig.equip(1, "vessel");
    rig.settleEquips();
    rig.tick();
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).max, 200.0, 1e-9);

    // Doubling per tier, all the way up: rare is four times common.
    Rig rare;
    rare.equip(0, "vessel", Rarity::Rare);
    rare.settleEquips();
    rare.tick();
    CHECK_NEAR(rare.world.get<ManaPool>(rare.player).max, 400.0, 1e-9);
    Rig apex;
    apex.equip(0, "vessel", Rarity::Apex);
    apex.settleEquips();
    apex.tick();
    CHECK_NEAR(apex.world.get<ManaPool>(apex.player).max, 100.0 * 512.0, 1e-9);
}

TEST(taking_the_pool_off_spills_the_mana_rather_than_banking_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.settleEquips();
    rig.tick();
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 40.0, 1e-9);

    rig.world.get<Loadout>(rig.player).slots[0] = LoadoutSlot{};
    rig.settleEquips();
    rig.tick();
    const ManaPool& empty = rig.world.get<ManaPool>(rig.player);
    CHECK_NEAR(empty.max, 0.0, 1e-9);
    CHECK_NEAR(empty.current, 0.0, 1e-9);

    // And putting it back on is not a refill: the once-ever seeding is what
    // keeps unequip/re-equip from being a free potion.
    rig.equip(0, "vessel");
    rig.settleEquips();
    rig.tick();
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).max, 100.0, 1e-9);
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 0.0, 1e-9);
}

TEST(a_magic_leaf_refills_the_pool_per_second_and_stops_at_the_ceiling) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "magicleaf");
    rig.settleEquips();
    rig.tick();
    ManaPool& pool = rig.world.get<ManaPool>(rig.player);
    pool.current = 0.0;

    // Five a second at common, and no faster: the regen is summed per SLOT.
    rig.tick(static_cast<int>(1000.0 / net::kTickMillis));
    CHECK_NEAR(pool.current, 5.0, 0.2);

    // Doubled one tier up, like every other mana figure.
    rig.equip(1, "magicleaf", Rarity::Uncommon);
    rig.settleEquips();
    rig.world.get<ManaPool>(rig.player).current = 0.0;
    rig.tick(static_cast<int>(1000.0 / net::kTickMillis));
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 10.0, 0.4);

    // And it stops at the ceiling rather than banking past it.
    rig.tick(600);
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 100.0, 1e-9);
}

TEST(an_orb_charges_homes_and_restores_mana_only_while_the_pool_is_short) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "orb");
    rig.settleEquips();
    rig.tick();
    ManaPool& pool = rig.world.get<ManaPool>(rig.player);
    pool.current = 50.0;

    // Past the orb's 1000ms charge: it flies home on its own, delivers one
    // burst, and enters the normal break cooldown -- the rose's path exactly.
    CHECK(rig.tickUntil([&] {
        return rig.slot(1).broken && rig.world.get<ManaPool>(rig.player).current >= 60.0;
    }));
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 60.0, 1e-9);

    // A full pool never calls it home: an orb that spent itself for nothing
    // would be a petal missing from the ring for no gain.
    CHECK(rig.tickUntil([&] { return !rig.slot(1).broken; }));
    rig.world.get<ManaPool>(rig.player).current = 100.0;
    rig.tick(80);
    CHECK(!rig.slot(1).broken);
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));
}

TEST(a_magic_missile_is_paid_for_when_it_spawns_and_fires_for_free) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "magicmissile");
    const auto mana = [&] { return rig.world.get<ManaPool>(rig.player).current; };
    rig.settleEquips();
    // The equip reload is a spawn like any other, and was paid for.
    CHECK_NEAR(mana(), 70.0, 1e-9);

    // The shot itself takes nothing.
    rig.setFlags(net::InputAttack);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) >= 1; }));
    CHECK_NEAR(mana(), 70.0, 1e-9);

    // Each reload is paid as the petal comes back: 70 -> 40 -> 10, and 10
    // cannot buy a fourth. That one waits off the ring, broken, and waits
    // WITHOUT taking what is left: a partial payment would drain the pool for
    // a petal that never came back.
    CHECK(rig.tickUntil([&] { return mana() <= 10.0; }, 4000));
    rig.setFlags(0);
    rig.tick(400);
    CHECK_NEAR(mana(), 10.0, 1e-9);
    CHECK(rig.slot(1).broken);
    CHECK(rig.petals(1).empty());

    // Funded, it is back on the very next tick, and paid for.
    rig.world.get<ManaPool>(rig.player).current = 100.0;
    rig.tick();
    CHECK(!rig.slot(1).broken);
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));
    CHECK_NEAR(mana(), 70.0, 1e-9);
}

TEST(a_berry_comes_back_only_once_its_reload_is_paid_for) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "berries");
    rig.settleEquips();
    ManaPool& pool = rig.world.get<ManaPool>(rig.player);
    pool.current = 100.0;
    CHECK(rig.tickUntil([&] { return rig.petals(1).size() == 4; }));

    // One volley spends all four berries.
    pool.current = 15.0;
    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(4));
    rig.setFlags(0);

    // Their 50 ms reload is long over, but 15 mana pays for one berry and
    // not two: the rest wait, and nothing is taken for them.
    rig.tick(30);
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 5.0, 1e-9);

    // Funded, the three that were waiting are back on the very next tick,
    // each one paid for.
    rig.world.get<ManaPool>(rig.player).current = 100.0;
    rig.tick();
    CHECK_EQ(rig.petals(1).size(), std::size_t(4));
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 70.0, 1e-9);
}

TEST(a_berry_reloads_on_its_own_short_cooldown_when_the_pool_can_pay) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "berries");
    rig.settleEquips();
    rig.world.get<ManaPool>(rig.player).current = 100.0;
    CHECK(rig.tickUntil([&] { return rig.petals(1).size() == 4; }));

    // Held attack with a full pool: the ring keeps firing, a volley every
    // few ticks rather than once a second, for 10 mana a berry.
    rig.setFlags(net::InputAttack);
    rig.tick(6);
    CHECK(rig.countOf(net::EntityKind::Projectile) >= 8);
    CHECK(rig.world.get<ManaPool>(rig.player).current <= 100.0 - 40.0 + 1e-9);
}

TEST(a_magic_bubble_is_paid_for_when_it_spawns_not_when_it_pops) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "magic_bubble");
    const auto mana = [&] { return rig.world.get<ManaPool>(rig.player).current; };
    rig.settleEquips();
    rig.tick();
    // The equip reload was a spawn, and was paid for.
    CHECK_NEAR(mana(), 60.0, 1e-9);

    // An empty pool does not hold the pop back: its mana is already spent.
    // The pop is momentum handed to the flower, and this rig steps no
    // movement, so the velocity it is left holding IS the burst.
    rig.world.get<ManaPool>(rig.player).current = 0.0;
    rig.setFlags(net::InputDefend);
    rig.setMove(0.0);
    rig.tick();
    CHECK(rig.velocity(rig.player).length() > 1.0);
    CHECK_NEAR(mana(), 0.0, 1e-9);

    // The next one is what cannot be paid for. Its reload runs out and the
    // slot waits broken, taking nothing from a pool that cannot cover it.
    rig.setFlags(0);
    rig.world.get<ManaPool>(rig.player).current = 10.0;
    rig.tick(100);
    CHECK(rig.slot(1).broken);
    CHECK(rig.petals(1).empty());
    CHECK_NEAR(mana(), 10.0, 1e-9);

    // Funded, it is back at once.
    rig.world.get<ManaPool>(rig.player).current = 100.0;
    rig.tick();
    CHECK(!rig.slot(1).broken);
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));
    CHECK_NEAR(mana(), 60.0, 1e-9);
}

TEST(a_magic_bubble_throws_the_flower_the_way_it_is_asking_to_go) {
    if (!contentLoaded()) return;
    // North, whichever side of the ring the petal happens to be orbiting on.
    // A plain bubble's bearing is the ring's; this one's is the player's.
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "magic_bubble");
    rig.settleEquips();
    rig.tick();
    const double heading = -kPi * 0.5;
    rig.setFlags(net::InputDefend);
    rig.setMove(heading);
    rig.tick();

    // Measured as the impulse the burst handed the flower, not as ground
    // covered: the pop is spent over the following half second by the movement
    // this rig deliberately does not run.
    const Vec2 thrown = rig.velocity(rig.player);
    CHECK(thrown.lengthSq() > 0.0);
    CHECK_NEAR(angularGap(thrown.angle(), heading), 0.0, 1e-9);

    // An analogue stick barely pushed dashes just as far, and in the same
    // direction: the bearing is read, the strength is not. On a rig of its own
    // so the measurement is not taken off a flower still gliding from the
    // first dash.
    Rig nudged;
    nudged.equip(0, "vessel");
    nudged.equip(1, "magic_bubble");
    nudged.settleEquips();
    nudged.tick();
    nudged.setFlags(net::InputDefend);
    nudged.setMove(heading, 0.05);
    nudged.tick();
    const Vec2 nudge = nudged.velocity(nudged.player);
    CHECK_NEAR(nudge.length(), thrown.length(), 1e-9);
    CHECK_NEAR(angularGap(nudge.angle(), heading), 0.0, 1e-9);
}

TEST(a_magic_bubble_with_no_direction_asked_for_is_held_not_spent) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "magic_bubble");
    rig.settleEquips();
    rig.tick();

    // Defend held, nothing asked for: the petal stays in the ring at full
    // charge. Only its spawn was paid for.
    rig.setFlags(net::InputDefend);
    rig.setMove(0.0, 0.0);
    rig.tick(20);
    CHECK_NEAR(rig.velocity(rig.player).length(), 0.0, 1e-9);
    CHECK(!rig.slot(1).broken);
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 60.0, 1e-9);

    // And it goes the instant a key is pressed, for nothing more.
    rig.setMove(kPi);
    rig.tick();
    CHECK(rig.velocity(rig.player).length() > 1.0);
    CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, 60.0, 1e-9);
}


TEST(a_ring_of_bubbles_stacks_into_one_pop_instead_of_cancelling) {
    if (!contentLoaded()) return;
    // Every bubble shoves the flower clear of ITSELF, so bearings read off one
    // standing centre cancel on a symmetric ring and a ring of bubbles would
    // do nothing at all. They chain instead: each pop is aimed from where the
    // ones before it have already thrown the flower.
    //
    // Popped from their places on a settled ring, as in play. Two opposite
    // bubbles line up exactly; four come out a little short of four pops,
    // because the side bubbles sit off the line the first one threw the
    // flower along and push partly across it.
    const auto reachOf = [](int bubbles) {
        Rig rig;
        rig.withMovement();
        for (int i = 0; i < bubbles; ++i) rig.equip(i, "bubble");
        rig.settleEquips();
        rig.settleRing();
        const Vec2 before = rig.position(rig.player);
        rig.setFlags(net::InputDefend);
        rig.tick();
        rig.setFlags(0);
        rig.tick(90);
        return distance(before, rig.position(rig.player));
    };
    const double one = reachOf(1);
    CHECK_NEAR(one, kBubblePopDistance, 1.0);
    CHECK_NEAR(reachOf(2), one * 2.0, 2.0);
    const double four = reachOf(4);
    CHECK(four > one * 3.5);
    CHECK(four <= one * 4.0 + 1.0);
}

TEST(a_stacked_bubble_pop_retains_momentum_above_the_old_cap) {
    if (!contentLoaded()) return;
    // A whole ring of the best bubbles asks for more ground in a tick than
    // stepCollide will carry -- it truncates the rest, and the client snaps
    // rather than eases a flower that jumped that far. The cap is what keeps
    // the stack a launch instead of a jump cut.
    Rig rig;
    rig.withMovement();
    for (int i = 0; i < kLoadoutActiveSlots; ++i) rig.equip(i, "bubble", Rarity::Apex);
    rig.settleEquips();
    rig.setFlags(net::InputDefend);
    rig.tick();
    // The petal's cap, written from the same movement constants it is written
    // from: the floor of the per-tick substep budget.
    const double budget = kMinSubstepLength * kMaxSubstepCount;
    CHECK(rig.velocity(rig.player).length() > budget / net::kTickSeconds);

    rig.setFlags(0);
    Vec2 at = rig.position(rig.player);
    double moved = 0.0;
    for (int t = 0; t < 90; ++t) {
        rig.tick();
        const Vec2 now = rig.position(rig.player);
        const double step = distance(at, now);
        CHECK(std::isfinite(step));
        moved += step;
        at = now;
    }
    // And it is a real launch, not a cap that swallowed the pop.
    CHECK(moved > 500.0);
}

TEST(defending_pops_a_bubble_and_pushes_the_flower) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.withMovement();
    rig.equip(0, "bubble");
    rig.settleEquips();
    const Vec2 before = rig.position(rig.player);
    rig.setFlags(net::InputDefend);
    // The pop is MOMENTUM, not a jump. The ring is stepped after movement, so
    // the tick it goes off hands the flower a velocity and moves it nowhere;
    // the reach is spent over the half second friction takes to eat that.
    rig.tick();
    CHECK(rig.velocity(rig.player).length() > 0.0);

    rig.setFlags(0);
    rig.tick();
    const double firstStep = distance(before, rig.position(rig.player));
    CHECK(firstStep > 0.0);
    CHECK(firstStep < kBubblePopDistance * 0.4);
    // Spent, and off the ring, the same tick the reference spends it.
    CHECK(rig.slot(0).broken);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));

    // The whole pop arrives, and the flower comes to rest rather than coasting.
    rig.tick(60);
    CHECK_NEAR(distance(before, rig.position(rig.player)), kBubblePopDistance, 1.0);
    CHECK_NEAR(rig.velocity(rig.player).length(), 0.0, 0.01);
}

TEST(every_bubble_throws_the_flower_gardns_distance_at_every_tier) {
    if (!contentLoaded()) return;
    // gardn's reach, not a ladder: a better bubble reloads faster and throws
    // no further, and the magic one dashes as far as the plain one pops.
    // Measured as the impulse the pop hands a standing flower, which is what
    // the reach is spent from.
    const auto impulseOf = [](const char* id, Rarity tier) {
        Rig rig;
        rig.equip(0, "vessel");
        rig.equip(1, id, tier);
        rig.settleEquips();
        rig.tick();
        rig.setFlags(net::InputDefend);
        rig.setMove(0.0);
        rig.tick();
        return rig.velocity(rig.player).length();
    };
    const double common = impulseOf("bubble", Rarity::Common);
    CHECK(common > 0.0);
    CHECK_NEAR(impulseOf("bubble", Rarity::Legendary), common, 1e-9);
    CHECK_NEAR(impulseOf("bubble", Rarity::Apex), common, 1e-9);
    CHECK_NEAR(impulseOf("magic_bubble", Rarity::Common), common, 1e-9);
}

TEST(a_bubble_pop_never_covers_its_reach_in_one_tick) {
    if (!contentLoaded()) return;
    // The glide is the point, so it is asserted as a shape and not just as a
    // total: every tick moves the flower less than the one before it, and no
    // single tick is the whole pop.
    Rig rig;
    rig.withMovement();
    rig.equip(0, "bubble");
    rig.settleEquips();
    rig.setFlags(net::InputDefend);
    rig.tick();
    rig.setFlags(0);

    double previous = std::numeric_limits<double>::infinity();
    int moving = 0;
    Vec2 at = rig.position(rig.player);
    for (int i = 0; i < 20; ++i) {
        rig.tick();
        const Vec2 now = rig.position(rig.player);
        const double step = distance(at, now);
        at = now;
        if (step < 0.05) break;
        CHECK(step < previous);
        previous = step;
        ++moving;
    }
    // Half a second of travel at 30 Hz, give or take: a handful of ticks is a
    // teleport with a tail, not a launch.
    CHECK(moving >= 8);
}

TEST(attacking_throws_one_web_and_consumes_its_petal) {
    if (!contentLoaded()) return;Rig rig;rig.equip(0,"web");rig.freezeRing();rig.settleEquips();rig.settleRing();
    rig.setFlags(net::InputAttack);rig.tick();const Entity spent=rig.petals(0).front();
    const Vec2 launch=rig.position(spent);const double bearing=rig.angleOf(spent);
    Entity launched=NULL_ENTITY;Query<GroundEffect,WebFlight,Transform> fields{rig.world};
    CHECK_EQ(fields.count(),1u);
    fields.each([&](Entity e,GroundEffect& field,WebFlight& flight,Transform& at){
        launched=e;CHECK_EQ(field.kind,GroundEffectKind::Web);CHECK_NEAR(field.radius,0,1e-9);
        CHECK_NEAR(flight.radius,90,1e-9);CHECK_NEAR(flight.duration,.35,1e-9);
        CHECK_NEAR(distance(at.position,launch),0,1e-6);
        CHECK_NEAR(distance(flight.to,launch),620,1e-6);
        CHECK_NEAR(angularGap((flight.to-launch).angle(),bearing),0,1e-6);
    });
    CHECK(rig.world.has<Dead>(spent));int moving=0;
    for(int i=0;i<static_cast<int>(std::ceil(.45/net::kTickSeconds));++i){rig.tick();if(launched!=NULL_ENTITY&&rig.world.has<WebFlight>(launched))++moving;}
    CHECK(moving>=8);CHECK(launched!=NULL_ENTITY);
    if(launched!=NULL_ENTITY){CHECK(!rig.world.has<WebFlight>(launched));CHECK_NEAR(rig.world.get<GroundEffect>(launched).radius,90,1e-9);}
    CHECK(rig.slot(0).broken);
}

TEST(electric_web_launches_lands_and_defends_with_damage_and_distinct_art_kind) {
    if (!contentLoaded()) return;
    for (const auto flag : {net::InputAttack, net::InputDefend}) {
        Rig rig;
        rig.equip(0, "electric_web", Rarity::Rare);
        rig.freezeRing(); rig.settleEquips(); rig.settleRing();
        rig.setFlags(flag); rig.tick();
        const Entity spent = rig.petals(0).front();
        CHECK(rig.world.has<Dead>(spent));
        Entity field = NULL_ENTITY;
        Query<GroundEffect, Transform, Replicated> fields{rig.world};
        CHECK_EQ(fields.count(), 1u);
        fields.each([&](Entity e, GroundEffect& effect, Transform&, Replicated& replicated) {
            field = e;
            CHECK_EQ(effect.kind, GroundEffectKind::ElectricWeb);
            CHECK_EQ(replicated.typeIndex, static_cast<std::uint16_t>(GroundEffectKind::ElectricWeb));
            CHECK_EQ(effect.owner, rig.player);
            CHECK(effect.damagePerHit > 0.0);
            CHECK_NEAR(effect.slowFactor, .5, 1e-9);
            CHECK_NEAR(effect.damageIntervalMillis, 1000.0, 1e-9);
        });
        if (field == NULL_ENTITY) continue;
        if (flag == net::InputAttack) {
            CHECK(rig.world.has<WebFlight>(field));
            const auto flight = rig.world.get<WebFlight>(field);
            CHECK_NEAR(distance(flight.from, flight.to), 620.0, 1e-6);
            CHECK_NEAR(rig.world.get<GroundEffect>(field).radius, 0.0, 1e-9);
            rig.reap(); rig.setFlags(0);
            rig.tick(static_cast<int>(std::ceil(.36 / net::kTickSeconds)));
            CHECK(!rig.world.has<WebFlight>(field));
            CHECK_NEAR(distance(rig.position(field), flight.to), 0.0, 1e-6);
            CHECK_NEAR(rig.world.get<GroundEffect>(field).radius, flight.radius, 1e-6);
        } else {
            CHECK(!rig.world.has<WebFlight>(field));
            CHECK(rig.world.get<GroundEffect>(field).radius > 90.0);
        }
        const Entity victim = addMob(rig, rig.position(field));
        CombatSystem combat;
        const double before = rig.healthOf(victim);
        ricochetCombatTick(rig, combat);
        CHECK(rig.healthOf(victim) < before);
        const double after = rig.healthOf(victim);
        ricochetCombatTick(rig, combat);
        CHECK_NEAR(rig.healthOf(victim), after, 1e-6);
        CHECK(rig.world.get<Afflictions>(victim).slowFactor < 1.0);
    }
}

TEST(defending_drops_pollen_with_discrete_damage_and_consumes_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "pollen", Rarity::Rare);
    rig.settleEquips();
    const Entity worn = rig.petals(0).front();
    rig.world.get<Health>(worn).current *= 0.5;
    const double wornHealth = rig.world.get<Health>(worn).current;
    rig.setFlags(net::InputDefend);
    rig.tick();

    Query<GroundEffect, Lifetime> fields{rig.world};
    // Rare pollen has two independently-consumed instances.
    CHECK_EQ(fields.count(), std::size_t(2));
    bool foundWorn = false;
    fields.each([&](Entity entity, GroundEffect& field, Lifetime& lifetime) {
        CHECK(rig.world.has<Health>(entity));
        const Health& health = rig.world.get<Health>(entity);
        CHECK(health.current <= health.max);
        if (std::abs(health.current - wornHealth) < 1e-9) foundWorn = true;
        CHECK_EQ(field.kind, GroundEffectKind::Poison);
        CHECK_NEAR(field.damagePerHit, 10.0 * petalStatScale(Rarity::Rare), 1e-9);
        CHECK_NEAR(field.damageIntervalMillis, 600.0, 1e-9);
        CHECK_NEAR(lifetime.remainingSeconds, 8.0, 1e-12);
    });
    CHECK(foundWorn);
    CHECK(rig.world.has<Dead>(rig.petals(0).front()));
}

TEST(uranium_lays_a_cloud_that_poisons_mobs_and_its_wearer) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "uranium", Rarity::Rare);
    rig.settleEquips();

    Query<GroundEffect, Lifetime> fields{rig.world};
    CHECK(rig.tickUntil([&] { return fields.count() > 0; }, 60));
    CHECK_EQ(fields.count(), std::size_t(1));
    fields.each([&](Entity, GroundEffect& field, Lifetime& lifetime) {
        CHECK_EQ(field.kind, GroundEffectKind::Radiation);
        CHECK_EQ(field.owner, rig.player);
        CHECK_NEAR(field.radius, 160.0, 1e-9);
        CHECK_NEAR(field.damagePerSecond, 4.0 * petalStatScale(Rarity::Rare), 1e-9);
        CHECK_NEAR(field.poisonDurationMillis, 2000.0, 1e-9);
        // The wearer takes the same rate the mobs do.
        CHECK_NEAR(field.ownerPoisonPerSecond, field.damagePerSecond, 1e-9);
        CHECK_NEAR(lifetime.remainingSeconds, 1.0, 1e-12);
    });
}

TEST(a_burst_heal_never_overshoots_the_health_bar) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "healer");
    rig.settleEquips();
    rig.world.get<Health>(rig.player).current = 105.0;
    rig.setFlags(net::InputDefend);
    rig.tick(60);
    CHECK_NEAR(rig.world.get<Health>(rig.player).current, 110.0, 1e-9);
}

TEST(a_projectile_petal_fires_its_fan_and_respects_its_cooldown) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peas");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    rig.tick(40);
    // Projectile timers do not advance while the ring is not attacking.
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(0));

    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(3));
    const double nextShotAt =
        rig.world.get<PetalInstance>(rig.petals(0).front()).nextProjectileMillis;
    CHECK_NEAR(nextShotAt, rig.now + 1000.0, 1e-9);

    const double petalHeading = rig.angleOf(rig.petals(0).front());
    std::vector<double> offsets;
    Query<ProjectileTag, Motion, Projectile> shots{rig.world};
    shots.each([&](Entity, ProjectileTag&, Motion& motion, Projectile& projectile) {
        offsets.push_back(wrapAngle(motion.velocity.angle() - petalHeading));
        CHECK_NEAR(motion.velocity.length(), 800.0, 1e-6);
        CHECK_NEAR(projectile.damage, 6.0, 1e-9);
        CHECK_NEAR(projectile.remainingDistance, 1000.0, 1e-9);
        CHECK(projectile.creditTo == rig.player);
        CHECK(projectile.owner == rig.player);
    });
    CHECK_EQ(offsets.size(), std::size_t(3));
    std::sort(offsets.begin(), offsets.end());
    // spreadAngle is the STEP between adjacent shots, and the fan is centred
    // on the petal's outward heading.
    CHECK_NEAR(offsets[0], -0.5, 1e-6);
    CHECK_NEAR(offsets[1], 0.0, 1e-6);
    CHECK_NEAR(offsets[2], 0.5, 1e-6);

    // No second volley until the cooldown is up, then exactly three more.
    rig.tick(20);
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(3));
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) > 3; }));
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(6));
}

TEST(a_defend_only_projectile_petal_fires_on_attack_and_on_defend) {
    // The shipped peas and grapes: defend-only keeps them in on attack, and
    // either input fires them. Behind the defend-only action gate they once
    // needed defend AND attack at once, which the input cannot say.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "pod");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));

    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(4));

    // Defending alone fires them too.
    Rig held;
    held.equip(0, "pod");
    held.freezeRing();
    held.settleEquips();
    held.settleRing();
    held.setFlags(net::InputDefend);
    held.tick();
    CHECK_EQ(held.countOf(net::EntityKind::Projectile), std::size_t(4));

    // Idle fires nothing.
    Rig idle;
    idle.equip(0, "pod");
    idle.freezeRing();
    idle.settleEquips();
    idle.settleRing();
    idle.tick(40);
    CHECK_EQ(idle.countOf(net::EntityKind::Projectile), std::size_t(0));
}

TEST(spaceship_fires_with_inverted_defend_and_requires_attack) {
    const auto& registry = testsupport::shippedContent();
    for (const Rarity rarity : {Rarity::Super, Rarity::Unique, Rarity::Apex}) {
        for (const auto attack : {int(net::InputAttack), net::InputAttack | net::InputDefend}) {
            Rig rig(registry);
            rig.equip(0, "spaceship", rarity);
            rig.settleEquips();
            Query<Projectile> shots{rig.world};
            rig.setFlags(net::InputDefend);
            rig.tick();
            CHECK_EQ(shots.count(), 0u);
            rig.setFlags(attack);
            rig.tick();
            CHECK_EQ(shots.count(), 1u);
            shots.each([&](Entity, Projectile& shot) {
                CHECK_EQ(shot.petalConfigIndex, registry.petalIndex("spaceship"));
                CHECK_EQ(shot.rarity, rarity);
            });
        }
    }
}

TEST(an_ordinary_projectile_petal_does_not_fire_on_defend) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peas");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    rig.setFlags(net::InputDefend);
    rig.tick(40);
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(0));
}

TEST(each_grain_of_a_clump_fires_along_its_own_facing) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peaclump");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    rig.setFlags(net::InputAttack);
    rig.tick();

    // Four grains on one ring place, so the volley is four shots -- and the
    // point of the test is that they leave in four DIFFERENT directions,
    // spaced a quarter turn apart around the clump, rather than stacking on
    // the slot's shared bearing.
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(4));
    const double slotBearing = rig.world.get<Transform>(rig.petals(0).front()).angle;
    std::vector<double> offsets;
    Query<ProjectileTag, Motion> shots{rig.world};
    shots.each([&](Entity, ProjectileTag&, Motion& motion) {
        offsets.push_back(wrapAngle(motion.velocity.angle() - slotBearing));
    });
    CHECK_EQ(offsets.size(), std::size_t(4));
    std::sort(offsets.begin(), offsets.end());
    CHECK_NEAR(offsets[0], -kTau * 0.25, 1e-6);
    CHECK_NEAR(offsets[1], 0.0, 1e-6);
    CHECK_NEAR(offsets[2], kTau * 0.25, 1e-6);
    CHECK_NEAR(std::abs(offsets[3]), kTau * 0.5, 1e-6);
}

TEST(firing_a_volley_spends_the_petal_and_pays_its_reload) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peas");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));

    rig.setFlags(net::InputAttack);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 3; }));

    // The shot IS the petal leaving the ring: the instance that fired is spent,
    // and the slot serves the reload a mob-killed petal would have served.
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    CHECK(rig.slot(0).broken);

    // Attack is still held, so the reload -- not the shot timer -- is what
    // paces the volleys: the slot comes back and spends itself again.
    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }, 400));
    CHECK(rig.tickUntil([&] { return rig.slot(0).broken; }, 400));
}

TEST(volley_cap_recounts_ended_projectiles_before_the_next_volley) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.system.volleyEntityLimit = 4;
    rig.equip(0, "peas");
    rig.settleEquips();
    const Entity previousShot = rig.world.create();
    rig.world.add<ProjectileTag>(previousShot);
    rig.world.add<Projectile>(previousShot).owner = rig.player;

    rig.setFlags(net::InputAttack);
    rig.tick(20);
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(0));

    rig.world.destroy(previousShot);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 3; }));
}

TEST(each_grain_of_a_clump_is_spent_by_its_own_shot) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peaclump");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));

    rig.setFlags(net::InputAttack);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 4; }));
    rig.tick();
    // Four grains, four shots, four spent grains -- each on its own per-instance
    // timer rather than on a slot-wide one.
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    // They serve that timer: a grain does not come straight back into another
    // volley the tick after it fired.
    rig.tick(10);
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(4));
    CHECK(rig.tickUntil([&] { return rig.petals(0).size() == 4; }, 400));
}

TEST(a_projectile_petal_with_no_health_pool_still_pays_its_reload) {
    if (!contentLoaded()) return;
    Rig rig;
    // An unbreakable emitter has no break path to fall through, so its reload
    // is stamped by the shot itself. Without that it would be respawned into
    // another volley on the very next tick.
    rig.equip(0, "emitter");
    rig.settleEquips();
    rig.setFlags(net::InputAttack);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 2; }));

    // Nothing else would take an unbreakable petal off the ring, so the shot
    // marks it Dead -- which the server reaps at the end of this tick, as it
    // does the spent web above -- and stamps the slot itself.
    const std::vector<Entity> spent = rig.petals(0);
    CHECK_EQ(spent.size(), std::size_t(1));
    CHECK(rig.world.has<Dead>(spent.front()));
    CHECK(rig.slot(0).broken);

    // Ten more ticks of held attack, and still exactly the one volley: without
    // the stamp the slot would respawn straight into the next one.
    rig.tick(10);
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(2));
}

TEST(a_projectile_petal_that_breaks_stops_firing) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "peas");
    rig.setFlags(net::InputAttack);
    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 3; }));
    rig.damage(rig.petals(0).front(), 5.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(0));
    rig.tick(20);
    CHECK_EQ(rig.countOf(net::EntityKind::Projectile), std::size_t(3));
}

TEST(summons_outlive_the_egg_that_broke_under_them) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    std::vector<Entity> summoned = pets.collect();
    CHECK_EQ(summoned.size(), std::size_t(2));
    for (const Entity pet : summoned) {
        CHECK(rig.world.get<Pet>(pet).owner == rig.player);
        CHECK_EQ(int(rig.world.get<Pet>(pet).slot), 0);
        CHECK(rig.world.has<MobTag>(pet));
        // Fights for the player, and is worth nothing to whoever kills it.
        CHECK(rig.world.get<Faction>(pet).team == Team::Players);
        CHECK(!rig.world.has<Bounty>(pet));
    }

    // No runaway summoning: the cap holds tick after tick.
    rig.tick(100);
    CHECK_EQ(rig.petCount(), std::size_t(2));

    rig.damage(rig.petals(0).front(), 4.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    // The squad STAYS. A pet is despawned only when the slot's petal actually
    // changes, when a duplicate replaces it, or when it gets lost (too far or
    // trapped) -- never by the egg breaking in combat -- and the
    // reload then finds the pets still out and hatches nothing.
    CHECK_EQ(rig.petCount(), std::size_t(2));
    // Not killed either: no corpse is left for the reaper to award.
    Query<Dead> dead{rig.world};
    CHECK_EQ(dead.count(), std::size_t(0));

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    rig.tick(5);
    CHECK_EQ(rig.petCount(), std::size_t(2));
}

TEST(a_pet_killed_in_the_field_is_resummoned) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    const Entity victim = pets.collect().front();
    rig.world.destroy(victim);
    CHECK_EQ(rig.petCount(), std::size_t(1));

    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
}

TEST(a_sandstorm_pet_is_never_retired_for_distance) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    // The retire pass reads the pet's brain, not its config, so the fixture's
    // critters can stand in for a stick's storms.
    Query<Pet> pets{rig.world};
    const Vec2 far = rig.position(rig.player) + Vec2{kViewportWidth * 4.0, 0.0};
    for (const Entity pet : pets.collect()) {
        rig.world.get<MobAi>(pet).kind = AiKind::Sandstorm;
        rig.world.get<Transform>(pet).position = far;
    }

    rig.tick(100);
    // Kept, and the egg was not charged a reload for them: the leash in the
    // mob AI is what brings a storm back now.
    CHECK_EQ(rig.petCount(), std::size_t(2));
    CHECK(!rig.slot(0).broken);
}

TEST(pet_recovery_preserves_the_same_far_summon_and_egg_cooldown) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    const Entity lost = pets.collect().front();
    rig.world.get<Transform>(lost).position =
        rig.position(rig.player) + Vec2{kPetRecallDistance + 10.0, 0.0};
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(2));
    CHECK(rig.world.isAlive(lost));
    CHECK(!rig.slot(0).broken);
    CHECK(distance(rig.position(lost), rig.position(rig.player)) < kPetHomeDistance);
    // Destroyed, not killed: nothing for the reaper to award.
    Query<Dead> dead{rig.world};
    CHECK_EQ(dead.count(), std::size_t(0));
}

TEST(a_pet_just_inside_the_recall_distance_is_kept) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        rig.world.get<Transform>(pet).position =
            rig.position(rig.player) + Vec2{kPetRecallDistance - 10.0, 0.0};
    }
    rig.tick(10);
    CHECK_EQ(rig.petCount(), std::size_t(2));
    CHECK(!rig.slot(0).broken);
}

TEST(pet_recovery_preserves_health_and_lifetime_of_trapped_summons) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    const Entity trapped = pets.collect().front();
    rig.world.get<MobAi>(trapped).petRecall = true;
    rig.world.get<Health>(trapped).current = 1;
    rig.world.get<Pet>(trapped).expiresAtMillis = rig.now + 10000;
    const double expires = rig.world.get<Pet>(trapped).expiresAtMillis;
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(2));
    CHECK(!rig.slot(0).broken);
    CHECK(!rig.world.get<MobAi>(trapped).petRecall);
    CHECK_NEAR(rig.world.get<Health>(trapped).current, 1, 0);
    CHECK_NEAR(rig.world.get<Pet>(trapped).expiresAtMillis, expires, 0);
}

TEST(pet_recovery_waits_for_safe_ground_instead_of_clipping_through_walls) {
    if (!contentLoaded()) return;
    Rig rig; rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
    Query<Pet> pets{rig.world}; const Entity pet = pets.collect().front();
    const Vec2 far = rig.position(rig.player) + Vec2{kPetRecallDistance + 100,0};
    rig.world.get<Transform>(pet).position = far;
    rig.world.get<Health>(pet).current = 1;
    rig.useHatchTerrain = true;
    const int x = Terrain::toTileCoord(rig.position(rig.player).x);
    const int y = Terrain::toTileCoord(rig.position(rig.player).y);
    for (int dx=-3; dx<=3; ++dx) for (int dy=-3; dy<=3; ++dy) rig.terrain.setTile(x+dx,y+dy,Tile::Solid);
    rig.tick();
    CHECK(rig.world.isAlive(pet)); CHECK_NEAR(rig.world.get<Health>(pet).current,1,0);
    CHECK(distanceSq(rig.position(pet),far)<1e-8);
    for (int dx=-3; dx<=3; ++dx) for (int dy=-3; dy<=3; ++dy) rig.terrain.setTile(x+dx,y+dy,Tile::Open);
    rig.tick();
    CHECK(distance(rig.position(pet),rig.position(rig.player))<kPetHomeDistance);
    CHECK(!rig.terrain.resolveWall(rig.position(pet),rig.world.get<Body>(pet).radius,Realm::Overworld).collided);
    CHECK_NEAR(rig.world.get<Health>(pet).current,1,0);
}

TEST(a_summoned_ring_mob_wears_its_seeds_on_its_owners_side) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "puffball_egg");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }));

    Query<Pet> pets{rig.world};
    const Entity pet = pets.collect().front();
    CHECK(rig.world.has<MobPetalRing>(pet));
    const MobPetalRing& ring = rig.world.get<MobPetalRing>(pet);
    CHECK_EQ(ring.seats.size(), std::size_t(4));
    for (const Entity seed : ring.seats) {
        CHECK(rig.world.isAlive(seed));
        CHECK(rig.world.has<MobRingPetal>(seed));
        CHECK(rig.world.get<MobRingPetal>(seed).mob == pet);
        // Its owner's side, so the seeds cut wild mobs and never the flower.
        CHECK(rig.world.get<Faction>(seed).team == Team::Players);
    }
}

TEST(a_summoned_centipede_arrives_whole_and_leaves_whole) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "centipede_egg");
    const std::size_t whole = 1 + static_cast<std::size_t>(kCentipedeSegmentCount);
    CHECK(rig.tickUntil([&] { return rig.petCount() == whole; }));

    const std::uint16_t head = fixture().registry.mobIndex("centipede");
    const std::uint16_t body = fixture().registry.mobIndex("centipede_body");
    Entity headEntity = NULL_ENTITY;
    int segments = 0;
    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        CHECK(rig.world.get<Pet>(pet).owner == rig.player);
        CHECK(rig.world.has<BodySegment>(pet));
        const std::uint16_t type = rig.world.get<MobType>(pet).configIndex;
        if (type == head) {
            headEntity = pet;
        } else {
            CHECK_EQ(type, body);
            ++segments;
        }
    }
    CHECK(headEntity != NULL_ENTITY);
    CHECK_EQ(segments, kCentipedeSegmentCount);
    // One linked animal: walking back from the head reaches every segment.
    int walked = 0;
    for (Entity at = rig.world.get<BodySegment>(headEntity).behind; at != NULL_ENTITY;
         at = rig.world.get<BodySegment>(at).behind) {
        CHECK(rig.world.get<BodySegment>(at).chainHead == headEntity);
        ++walked;
    }
    CHECK_EQ(walked, kCentipedeSegmentCount);
    // And a body counts toward nothing: the egg's squad is one centipede.
    rig.tick(30);
    CHECK_EQ(rig.petCount(), whole);

    rig.unequip(0);
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(0));
}

TEST(a_summoned_queen_hatches_her_minions_as_her_owners_timed_pets) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "matriarch_egg");
    CHECK(rig.tickUntil([&] { return rig.petCount() >= 2; }));

    const std::uint16_t critter = fixture().registry.mobIndex("critter");
    std::size_t minions = 0;
    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        if (rig.world.get<MobType>(pet).configIndex != critter) continue;
        ++minions;
        CHECK(rig.world.get<Pet>(pet).owner == rig.player);
        CHECK(rig.world.get<Pet>(pet).expiresAtMillis > 0.0);
        CHECK(rig.world.get<Faction>(pet).team == Team::Players);
        CHECK(!rig.world.has<Bounty>(pet));
    }
    CHECK(minions >= 1);

    // Up to her maxAlive and no further, however long she stands there.
    std::size_t most = 0;
    for (int i = 0; i < 300; ++i) {
        rig.tick();
        std::size_t live = 0;
        for (const Entity pet : pets.collect()) {
            if (rig.world.get<MobType>(pet).configIndex == critter) ++live;
        }
        most = std::max(most, live);
    }
    CHECK_EQ(most, std::size_t(3));
}

std::size_t timedPetCount(World& world) {
    std::size_t n = 0;
    Query<Pet> query{world};
    query.each([&](Entity, Pet& pet) {
        if (pet.expiresAtMillis > 0.0) ++n;
    });
    return n;
}

TEST(a_timed_summoner_hatches_one_per_reload_until_the_mana_runs_out) {
    if (!contentLoaded()) return;
    Rig rig;
    // 100 mana and nothing refilling it: five storms' worth, and no sixth.
    rig.equip(0, "vessel");
    rig.equip(1, "stormcaller");
    rig.settleEquips();
    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }));
    // No squad cap: the next reload hatches another beside the first.
    const double first = rig.now;
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }, 40));
    CHECK_NEAR(rig.now - first, 1000.0, net::kTickMillis * 1.5);
    CHECK(rig.tickUntil([&] { return rig.petCount() == 5; }));
    CHECK(rig.world.get<ManaPool>(rig.player).current < 20.0);
    rig.tick(90);
    CHECK_EQ(rig.petCount(), std::size_t(5));
    CHECK_EQ(timedPetCount(rig.world), std::size_t(5));

    // Each runs out twenty seconds after it was hatched, uncharged to the
    // petal, and is not killed doing so.
    CHECK(rig.tickUntil([&] { return rig.petCount() < 5; }, 30 * 20));
    CHECK_NEAR(rig.now - first, 20000.0, net::kTickMillis * 1.5);
    CHECK(!rig.slot(1).broken);
    Query<Dead> dead{rig.world};
    CHECK_EQ(dead.count(), std::size_t(0));
    CHECK(rig.tickUntil([&] { return rig.petCount() == 0; }, 30 * 10));
}

TEST(a_timed_summons_price_doubles_per_tier) {
    if (!contentLoaded()) return;
    Rig rig;
    // A rare stormcaller costs 80 a storm: one from a 100-mana pool.
    rig.equip(0, "vessel");
    rig.equip(1, "stormcaller", Rarity::Rare);
    rig.settleEquips();
    CHECK_NEAR(fixture().registry.petalStats(petalId("stormcaller"), Rarity::Rare).petManaCost,
               80.0, 1e-9);
    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }));
    rig.tick(150);
    CHECK_EQ(rig.petCount(), std::size_t(1));
}

TEST(a_squad_of_the_same_mob_neither_counts_nor_recalls_timed_summons) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "vessel");
    rig.equip(1, "stormcaller");
    rig.settleEquips();
    CHECK(rig.tickUntil([&] { return timedPetCount(rig.world) == 3; }));
    // The summoner hatches critters too. Its squad of two comes out whole
    // beside the storms, and replacing it does not take them back.
    rig.equip(2, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() - timedPetCount(rig.world) == 2; }));
    CHECK(timedPetCount(rig.world) >= 3);
    rig.unequip(2);
    rig.tick();
    CHECK_EQ(rig.petCount(), timedPetCount(rig.world));
    CHECK(timedPetCount(rig.world) >= 3);
}

TEST(eggs_of_one_type_stack_a_squad_each) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    rig.equip(1, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 4; }));
    rig.tick(60);
    CHECK_EQ(rig.petCount(), std::size_t(4));
    int fromFirst = 0;
    int fromSecond = 0;
    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        const int slot = rig.world.get<Pet>(pet).slot;
        if (slot == 0) ++fromFirst;
        if (slot == 1) ++fromSecond;
    }
    CHECK_EQ(fromFirst, 2);
    CHECK_EQ(fromSecond, 2);

    // Losing one of the second egg's pair rebuilds that pair alone, and leaves
    // the first egg's squad standing.
    std::vector<Entity> first;
    for (const Entity pet : pets.collect()) {
        if (rig.world.get<Pet>(pet).slot == 0) first.push_back(pet);
    }
    for (const Entity pet : pets.collect()) {
        if (rig.world.get<Pet>(pet).slot == 1) {
            rig.world.destroy(pet);
            break;
        }
    }
    CHECK(rig.tickUntil([&] { return rig.petCount() == 4; }));
    for (const Entity pet : first) CHECK(rig.world.isAlive(pet));

    // Unequipping one egg takes back its own pair only.
    rig.unequip(1);
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(2));
    for (const Entity pet : first) CHECK(rig.world.isAlive(pet));
}

TEST(a_lost_pet_reloads_the_egg_that_hatched_it_and_no_other) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    rig.equip(1, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 4; }));

    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        if (rig.world.get<Pet>(pet).slot == 1) {
            rig.world.get<MobAi>(pet).petRecall = true;
            break;
        }
    }
    rig.tick();
    CHECK(!rig.slot(0).broken);
    CHECK(rig.slot(1).broken);
}

TEST(a_row_of_eggs_stops_at_thirty_pets) {
    if (!contentLoaded()) return;
    Rig rig;
    // Two from a common egg, then six an apex egg: 2 + 6 * 4 = 26 leaves the
    // next apex egg room for four of its six, and the rest none.
    rig.equip(0, "summoner");
    for (int i = 1; i < kLoadoutActiveSlots; ++i) rig.equip(i, "summoner", Rarity::Apex);
    CHECK(rig.tickUntil([&] { return rig.petCount() == 30; }));
    // And it holds there: the same thirty, not a squad cut short by the cap
    // being recalled and re-hatched every tick.
    Query<Pet> pets{rig.world};
    const std::vector<Entity> before = pets.collect();
    rig.tick(100);
    CHECK_EQ(rig.petCount(), std::size_t(30));
    for (const Entity pet : before) CHECK(rig.world.isAlive(pet));
}

TEST(unequipping_a_summoner_recalls_its_pets) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
    rig.unequip(0);
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(0));
}

TEST(a_summoning_shot_brings_one_summon_per_landing_up_to_its_cap) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(2, "saucer");
    rig.settleEquips();
    // Sitting on the ring hatches nothing: it is not an egg.
    CHECK_EQ(rig.petCount(), std::size_t(0));

    const std::uint16_t saucer = petalId("saucer");
    const auto land = [&](Vec2 at) {
        rig.system.landShots(rig.world, fixture().registry,
                             {ShotLanding{rig.player, 2, saucer, Rarity::Common,
                                          Realm::Overworld, at}},
                             nullptr);
    };
    const auto summons = [&] { return rig.world.get<PetalSlotState>(rig.player).shotSummons; };

    const Vec2 spots[] = {{1500, 1000}, {1000, 1500}, {500, 1000}};
    for (const Vec2 at : spots) land(at);
    CHECK_EQ(rig.petCount(), std::size_t(3));
    const std::vector<Entity> placed = summons();
    CHECK_EQ(placed.size(), std::size_t(3));
    if (placed.size() != 3) return;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const Entity pet = placed[i];
        const auto critter = fixture().registry.mobIndex("critter");
        CHECK_EQ(rig.world.get<MobType>(pet).configIndex, critter);
        CHECK_NEAR(rig.world.get<Health>(pet).max, fixture().registry.mob(critter).health * 5.0, 1e-9);
        CHECK_EQ(rig.world.get<Pet>(pet).owner, rig.player);
        CHECK_EQ(static_cast<int>(rig.world.get<Pet>(pet).slot), 2);
        // Where the shot landed, give or take the spread a squad lands with.
        const double spread = rig.world.get<Body>(pet).radius * 2.0 + kPlayerBaseRadius;
        CHECK(distance(rig.position(pet), spots[i]) <= spread + 1e-9);
    }

    // At the cap a landing moves the oldest there instead, and makes it the
    // newest: the next landing moves the one after it.
    land({1200, 1200});
    CHECK_EQ(rig.petCount(), std::size_t(3));
    CHECK_NEAR(rig.position(placed[0]).x, 1200.0, 1e-9);
    CHECK_NEAR(rig.position(placed[0]).y, 1200.0, 1e-9);
    CHECK_EQ(summons().back(), placed[0]);
    land({800, 800});
    CHECK_NEAR(rig.position(placed[1]).x, 800.0, 1e-9);
    CHECK_NEAR(rig.position(placed[1]).y, 800.0, 1e-9);
    CHECK_NEAR(rig.position(placed[0]).x, 1200.0, 1e-9);

    // A summon killed in the field frees its place for the next landing.
    rig.world.add<Dead>(placed[2]);
    rig.reap();
    land({1000, 600});
    CHECK_EQ(rig.petCount(), std::size_t(3));
    CHECK(rig.world.isAlive(placed[0]));
    CHECK(rig.world.isAlive(placed[1]));

    // The ring leaves them be while the petal stays on, and recalls them
    // with it.
    rig.tick(30);
    CHECK_EQ(rig.petCount(), std::size_t(3));
    rig.unequip(2);
    rig.tick();
    CHECK_EQ(rig.petCount(), std::size_t(0));
}

TEST(a_summoning_shot_landing_after_its_petal_came_off_summons_nothing) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(2, "basic");
    rig.settleEquips();
    // In flight when the saucer was swapped out for the basic.
    rig.system.landShots(rig.world, fixture().registry,
                         {ShotLanding{rig.player, 2, petalId("saucer"), Rarity::Common,
                                      Realm::Overworld, {1500, 1000}}},
                         nullptr);
    CHECK_EQ(rig.petCount(), std::size_t(0));
}

TEST(a_pet_is_smaller_than_the_wild_mob_of_its_tier) {
    if (!contentLoaded()) return;
    const ContentRegistry& content = fixture().registry;
    const std::uint16_t critter = content.mobIndex("critter");
    // Sized on the old, smaller mob ladder (kPetSizeScale), then the pet
    // ramp: the old size at common, a third of it by unique.
    for (const Rarity rarity : {Rarity::Common, Rarity::Epic, Rarity::Unique}) {
        Rig rig;
        rig.equip(0, "summoner", rarity);
        CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
        const std::size_t tier = static_cast<std::size_t>(ladderIndex(rarity));
        const double wild = content.mobStats(critter, rarity).radius;
        const double old = wild * kPetSizeScale[tier] / kMobSizeScale[tier];
        const double expected = old * mobSizeRamp(rarity, 1.0 / 3.0);
        if (rarity != Rarity::Common) { CHECK(expected < wild); }
        Query<Pet> pets{rig.world};
        for (const Entity pet : pets.collect()) {
            CHECK_NEAR(rig.world.get<Body>(pet).radius, expected, 1e-9);
        }
    }
}

TEST(a_pet_climbs_the_petal_ladder_not_the_mob_one) {
    if (!contentLoaded()) return;
    const ContentRegistry& content = fixture().registry;
    const std::uint16_t critter = content.mobIndex("critter");
    const MobConfig& config = content.mob(critter);
    // The egg's tier is the pet's tier, and a tier of egg is worth what a tier
    // of any other petal is: 3x, health and damage alike. Unique is the top a
    // pet reaches, and where the wild ladder is furthest from it.
    for (const Rarity rarity : {Rarity::Common, Rarity::Rare, Rarity::Unique}) {
        Rig rig;
        rig.equip(0, "summoner", rarity);
        CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
        const double ladder = petalStatScale(rarity);
        Query<Pet> pets{rig.world};
        for (const Entity pet : pets.collect()) {
            const Health& health = rig.world.get<Health>(pet);
            CHECK_NEAR(health.max, config.health * ladder * 5.0, 1e-9);
            CHECK_NEAR(health.current, health.max, 1e-9);
            CHECK_NEAR(rig.world.get<ContactDamage>(pet).amount, config.damage * ladder, 1e-9);
        }
    }
    // What the pet used to inherit, and what it no longer does: a unique
    // critter in the wild has far more health than the petal ladder gives.
    CHECK(content.mobStats(critter, Rarity::Unique).health >
          config.health * petalStatScale(Rarity::Unique) * 100.0);
}

TEST(a_universal_egg_hatches_the_apex_squad_with_sqrt2_its_strength) {
    if (!contentLoaded()) return;
    const ContentRegistry& content = fixture().registry;
    const MobConfig& config = content.mob(content.mobIndex("critter"));
    // An apex or universal summoner hatches three squads at its own tier.
    // The universal strength bonus also reaches both summon stats.
    for (const Rarity rarity : {Rarity::Apex, Rarity::Universal}) {
        Rig rig;
        rig.equip(0, "summoner", rarity);
        CHECK(rig.tickUntil([&] { return rig.petCount() == 6; }));
        rig.tick(5);
        CHECK_EQ(rig.petCount(), std::size_t(6));
        const auto& source = content.petal(content.petalIndex("summoner"));
        const double strength = petalStrengthScale(rarity) * hyperSummonScale(source, rarity) *
                                hyperSummonPromotionScale(source, rarity);
        Query<Pet> pets{rig.world};
        for (const Entity pet : pets.collect()) {
            CHECK(rig.world.get<MobType>(pet).rarity == rarity);
            const double health = rig.world.get<Health>(pet).max;
            const double damage = rig.world.get<ContactDamage>(pet).amount;
            CHECK_NEAR(health, config.health * strength * 5.0, config.health * strength * 5e-12);
            CHECK_NEAR(damage, config.damage * strength, config.damage * strength * 1e-12);
        }
    }
}

TEST(the_pet_health_talent_multiplies_pets_and_nothing_else) {
    if (!contentLoaded()) return;
    const MobConfig& critter = fixture().registry.mob(fixture().registry.mobIndex("critter"));
    const Rarity tier = Rarity::Legendary;
    const double talent = 2.0114;

    Rig rig;
    rig.world.add<PlayerSkillTree>(rig.player);
    rig.world.get<PlayerSkillTree>(rig.player).skills.set(SkillId::Summoner, rarityIndex(tier));
    rig.equip(0, "summoner");
    rig.equip(1, "basic");
    rig.settleEquips();
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        // The pool grows on the Petal Health talent's own table...
        CHECK_NEAR(rig.world.get<Health>(pet).max, critter.health * talent * 5.0, 1e-9);
        // ...and it is health alone: what the pet hits for is untouched.
        CHECK_NEAR(rig.world.get<ContactDamage>(pet).amount, critter.damage, 1e-9);
    }
    // The ring does not share in it. A basic petal is worth its own 10.
    const std::vector<Entity> basic = rig.petals(1);
    CHECK_EQ(basic.size(), std::size_t(1));
    if (!basic.empty()) CHECK_NEAR(rig.world.get<Health>(basic.front()).max, 10.0, 1e-9);
    // Nor does the flower.
    CHECK_NEAR(rig.world.get<Health>(rig.player).max, maxHealthForLevel(1), 1e-9);
}

TEST(both_health_talents_grant_what_their_tooltip_quotes) {
    if (!contentLoaded()) return;
    const MobConfig& critter = fixture().registry.mob(fixture().registry.mobIndex("critter"));
    // Both maximum branches multiply health by 4.0456 (petal HP is floored): the
    // server reads the very table the talent tooltip prints.
    Rig rig;
    rig.world.add<PlayerSkillTree>(rig.player);
    SkillSet& skills = rig.world.get<PlayerSkillTree>(rig.player).skills;
    skills.set(SkillId::PetalHealth, skillTierCount(SkillId::PetalHealth)-1);
    skills.set(SkillId::Summoner, skillTierCount(SkillId::Summoner)-1);
    rig.equip(0, "summoner");
    rig.equip(1, "basic");
    rig.settleEquips();
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));

    const std::vector<Entity> basic = rig.petals(1);
    CHECK_EQ(basic.size(), std::size_t(1));
    if (!basic.empty()) CHECK_NEAR(rig.world.get<Health>(basic.front()).max, 40.0, 1e-9);
    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        CHECK_NEAR(rig.world.get<Health>(pet).max, critter.health * 4.0456 * 5.0, 1e-9);
    }
}

TEST(a_petal_health_pool_is_rounded_to_a_whole_number) {
    if (!contentLoaded()) return;
    // Cotton's 2 under the epic tier's 135% is 2.7, and the petal gets 3.
    Rig rig;
    rig.world.add<PlayerSkillTree>(rig.player);
    rig.world.get<PlayerSkillTree>(rig.player)
        .skills.set(SkillId::PetalHealth, rarityIndex(Rarity::Epic));
    rig.equip(0, "cotton");
    rig.settleEquips();
    const std::vector<Entity> cotton = rig.petals(0);
    CHECK_EQ(cotton.size(), std::size_t(1));
    if (!cotton.empty()) CHECK_NEAR(rig.world.get<Health>(cotton.front()).max, 3.0, 1e-9);
}

TEST(the_petal_health_talent_leaves_pets_alone) {
    if (!contentLoaded()) return;
    const MobConfig& critter = fixture().registry.mob(fixture().registry.mobIndex("critter"));
    Rig rig;
    rig.world.add<PlayerSkillTree>(rig.player);
    rig.world.get<PlayerSkillTree>(rig.player)
        .skills.set(SkillId::PetalHealth, skillTierCount(SkillId::PetalHealth)-1);
    rig.equip(0, "summoner");
    CHECK(rig.tickUntil([&] { return rig.petCount() == 2; }));
    Query<Pet> pets{rig.world};
    for (const Entity pet : pets.collect()) {
        CHECK_NEAR(rig.world.get<Health>(pet).max, critter.health * 5.0, 1e-9);
    }
}

// ---------------------------------------------------------------------------
// Duplicator
// ---------------------------------------------------------------------------

/// Gives the rig's flower the tree a Duplicator node needs -- four
/// Loadout upgrades -- and the node itself: tier 0 Duplicator, 1 Triplicator.
void buyDuplicator(Rig& rig, int tier) {
    SkillSet& skills = rig.world.ensure<PlayerSkillTree>(rig.player).skills;
    skills.set(SkillId::Loadout, 3);
    skills.set(SkillId::Duplicator, tier);
}

TEST(icosahedron_equipped_copies_do_not_cancel_each_others_second_hit_protection) {
    // Both copies must survive their first recoil independently.
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "icosahedron");
    rig.equip(1, "icosahedron");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const std::array<Entity, 2> petals{rig.petals(0).front(), rig.petals(1).front()};
    const Vec2 start = rig.position(petals[0]);
    rig.world.get<Transform>(petals[1]).position = start;
    const Entity first = addMob(rig, start);
    const Entity second = addMob(rig, start + Vec2{100, 0});
    rig.world.add<ContactDamage>(first, ContactDamage{10000});
    rig.world.add<ContactDamage>(second, ContactDamage{10000});
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    rig.tick();
    for (const Entity petal : petals) {
        CHECK(!rig.world.has<Dead>(petal));
        CHECK(rig.world.isAlive(petal));
        CHECK_NEAR(rig.world.get<Health>(petal).current, 1.0, 1e-9);
    }
    CHECK(rig.tickUntil([&] {
        ricochetCombatTick(rig, combat);
        return rig.world.has<Dead>(petals[0]) && rig.world.has<Dead>(petals[1]);
    }, 60));
    for (const Entity petal : petals) {
        CHECK_EQ(rig.world.get<PetalInstance>(petal).ricochet.hits, 2);
    }
}

TEST(duplicator_adds_one_copy_to_a_clump_and_triplicator_two) {
    if (!contentLoaded()) return;
    for (int tier = 0; tier < 2; ++tier) {
        Rig rig;
        buyDuplicator(rig, tier);
        rig.equip(0, "sandy");
        rig.equip(1, "basic");
        rig.settleEquips();

        const int expected = 4 + (tier == 0 ? 1 : 2);
        const std::vector<Entity> grains = rig.petals(0);
        CHECK_EQ(grains.size(), static_cast<std::size_t>(expected));
        // Every grain knows the clump it belongs to is the bigger one, which is
        // what fans them evenly round its centre.
        for (const Entity grain : grains) {
            CHECK_EQ(int(rig.world.get<PetalInstance>(grain).subCount), expected);
        }
        // A single petal is not a clump, and is left alone.
        CHECK_EQ(rig.petals(1).size(), std::size_t(1));
    }
}

TEST(duplicator_without_four_loadout_upgrades_adds_nothing) {
    if (!contentLoaded()) return;
    Rig rig;
    buyDuplicator(rig, 1);
    rig.world.get<PlayerSkillTree>(rig.player)
        .skills.set(SkillId::Loadout, 2);
    rig.equip(0, "sandy");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));
}

TEST(a_duplicated_unclumped_petal_takes_a_ring_place_per_copy) {
    if (!contentLoaded()) return;
    // Uncommon pollen fields two, unclumped, so Duplicator makes it three
    // ring places -- with the anchor, the ring is quartered.
    Rig rig;
    buyDuplicator(rig, 0);
    rig.equip(0, "pollen", Rarity::Uncommon);
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const std::vector<Entity> pollen = rig.petals(0);
    CHECK_EQ(pollen.size(), std::size_t(3));
    const double wedge = kTau / 4.0;
    const double spin = rig.ring().spin;
    for (const Entity petal : rig.petals()) {
        const PetalInstance& instance = rig.world.get<PetalInstance>(petal);
        const int ordinal = instance.slot == 0 ? instance.subIndex : 3;
        CHECK_NEAR(angularGap(rig.angleOf(petal), spin + wedge * ordinal), 0.0, 1e-6);
    }
}

TEST(buying_or_resetting_duplicator_rebuilds_the_slots_it_resizes) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.world.add<PlayerSkillTree>(rig.player);
    rig.equip(0, "pollen", Rarity::Uncommon);   // two copies, one shared pool
    rig.equip(1, "sandy");                      // four copies, each its own
    rig.equip(2, "basic");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(2));
    CHECK_EQ(rig.petals(1).size(), std::size_t(4));
    const Entity basic = rig.petals(2).front();

    // Bought mid-life, under slots whose petal never changed. The shared pool
    // must come back whole -- not with the new copy charged as one just lost --
    // and the clump must grow a fifth grain on a timer sized for five.
    buyDuplicator(rig, 0);
    rig.settleEquips();
    const std::vector<Entity> pollen = rig.petals(0);
    CHECK_EQ(pollen.size(), std::size_t(3));
    const double pool = fixture().registry.petalStats(petalId("pollen"), Rarity::Uncommon).health;
    for (const Entity petal : pollen) {
        CHECK_NEAR(rig.world.get<Health>(petal).max, pool, 1e-9);
        CHECK_NEAR(rig.healthOf(petal), pool, 1e-9);
    }
    CHECK_EQ(rig.petals(1).size(), std::size_t(5));
    // A slot the talent does not resize keeps the petal it had.
    CHECK(rig.petals(2).front() == basic);

    // And back: a reset must take the extra grain away, not leave it orbiting
    // past the end of a clump that no longer counts it.
    rig.world.get<PlayerSkillTree>(rig.player).skills.clear();
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(2));
    CHECK_EQ(rig.petals(1).size(), std::size_t(4));
    for (const Entity grain : rig.petals(1)) {
        CHECK(rig.world.get<PetalInstance>(grain).subIndex < 4);
    }
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

/// A petal leaving the field -- broken, swapped out, or its flower down -- is
/// cued as Killed where it was, so every client in view plays its death
/// animation there, as gardn's does for every petal its server deletes.
TEST(a_petal_leaving_the_field_is_cued_as_killed_where_it_was) {
    if (!contentLoaded()) return;
    Rig rig;
    std::uint32_t counter = 0;
    rig.system.allocateNetId = [&counter] { return ++counter; };
    rig.equip(0, "basic");
    rig.equip(1, "basic");
    rig.equip(2, "basic");
    rig.settleEquips();
    const auto cue = [&](std::uint32_t netId) -> const WireEvent* {
        for (const WireEvent& e : rig.events.events()) {
            if (e.kind == net::EventKind::Killed && e.netId == netId) return &e;
        }
        return nullptr;
    };
    const auto idOf = [&](int slot) { return rig.world.get<NetId>(rig.petals(slot).front()).value; };

    const std::uint32_t broken = idOf(0);
    const Vec2 brokenAt = rig.position(rig.petals(0).front());
    rig.damage(rig.petals(0).front(), 10.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK(cue(broken) != nullptr);
    if (cue(broken) != nullptr) CHECK_NEAR((cue(broken)->position - brokenAt).length(), 0.0, 1e-9);

    const std::uint32_t swapped = idOf(1);
    rig.unequip(1);
    rig.tick();
    CHECK(cue(swapped) != nullptr);

    const std::uint32_t downed = idOf(2);
    rig.world.add<Dead>(rig.player, Dead{NULL_ENTITY});
    rig.tick();
    CHECK(cue(downed) != nullptr);
}


TEST(spawned_entities_take_net_ids_when_an_allocator_is_installed) {
    if (!contentLoaded()) return;
    Rig rig;
    std::uint32_t counter = 0;
    rig.system.allocateNetId = [&counter] { return ++counter; };
    rig.equip(0, "peas");
    rig.settleEquips();
    rig.setFlags(net::InputAttack);
    rig.tick();

    const Entity petal = rig.petals(0).front();
    CHECK(rig.world.has<NetId>(petal));
    CHECK_EQ(rig.world.get<NetId>(petal).value, std::uint32_t(1));
    CHECK_EQ(rig.world.get<Replicated>(petal).kind, net::EntityKind::Petal);
    CHECK_EQ(rig.world.get<Replicated>(petal).typeIndex, petalId("peas"));

    CHECK(rig.tickUntil([&] { return rig.countOf(net::EntityKind::Projectile) == 3; }));
    Query<ProjectileTag, NetId> shots{rig.world};
    CHECK_EQ(shots.count(), std::size_t(3));
    CHECK_EQ(counter, std::uint32_t(4));
}

TEST(petals_carry_no_motion_so_movement_cannot_fight_the_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    const Entity petal = rig.petals(0).front();
    CHECK(!rig.world.has<Motion>(petal));
    CHECK(!rig.world.has<Knockback>(petal));
    CHECK(rig.world.has<Body>(petal));
    CHECK(rig.world.has<ContactDamage>(petal));
    CHECK(rig.world.has<HitCooldowns>(petal));
    CHECK(rig.world.get<Faction>(petal).team == Team::Players);
}

TEST(the_ring_follows_the_flower_it_belongs_to) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.world.get<Transform>(rig.player).position = Vec2{5000.0, -2000.0};
    // The ring is sprung to its owner and centred on where that owner was at
    // the end of the LAST tick, so a flower that jumps five thousand units
    // drags its petals after it rather than teleporting them: they arrive over
    // the following ticks.
    rig.tick();
    CHECK(rig.radiusOf(rig.petals(0).front()) > rig.ring().radius * 10.0);
    rig.settleRing();
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()), rig.ring().radius, 1e-4);
    CHECK_NEAR(rig.position(rig.petals(0).front()).x - 5000.0,
               std::cos(rig.ring().spin) * rig.ring().radius, 1e-4);
}

TEST(two_players_keep_separate_rings) {
    if (!contentLoaded()) return;
    Rig rig;
    Rig other;   // a second world, to prove nothing here is process-wide state
    rig.equip(0, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    other.equip(0, "sandy");
    other.freezeRing();
    other.settleEquips();
    other.settleRing();
    CHECK_EQ(rig.petals().size(), std::size_t(2));    // basic and the anchor
    CHECK_EQ(other.petals().size(), std::size_t(5));  // four grains and the anchor

    // ...and a second flower in the SAME world.
    const Entity second = rig.world.create();
    rig.world.add<PlayerTag>(second);
    rig.world.add<Transform>(second, Transform{{-500.0, 400.0}, 0.0});
    rig.world.add<Body>(second, Body{kPlayerBaseRadius, 1.0});
    rig.world.add<Health>(second, Health{100.0, 100.0, 0.0, 0.0});
    rig.world.add<Faction>(second, Faction{Team::Players, false});
    rig.world.add<PlayerInput>(second);
    rig.world.add<PlayerModifiers>(second);
    rig.world.add<Loadout>(second);
    rig.world.add<PetalRing>(second);
    rig.world.get<Loadout>(second).slots[0].configIndex = petalId("shards");
    rig.settleRing();

    CHECK_EQ(rig.petals().size(), std::size_t(2));
    Query<PetalInstance> all{rig.world};
    CHECK_EQ(all.count(), std::size_t(5));            // plus the second flower's three shards
    for (const Entity petal : all.collect()) {
        const PetalInstance& instance = rig.world.get<PetalInstance>(petal);
        const Vec2 owner = rig.world.get<Transform>(instance.owner).position;
        // The second flower's ring still turns -- it wears no anchor -- so its
        // shards hold the steady orbit a spring settles into rather than the
        // ring radius exactly.
        const double slack = instance.owner == rig.player ? 1e-6 : 1.0;
        CHECK_NEAR((rig.world.get<Transform>(petal).position - owner).length(),
                   rig.world.get<PetalRing>(instance.owner).radius, slack);
    }
}


// ---------------------------------------------------------------------------
// The fraction the loadout bar drains its tile by
// ---------------------------------------------------------------------------

TEST(a_hurt_petal_reports_a_part_full_slot) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.settleEquips();
    CHECK_NEAR(rig.slotHealth(0), 1.0, 1e-9);

    rig.damage(rig.petals(0).front(), 4.0);
    rig.tick();
    CHECK_NEAR(rig.slotHealth(0), 0.6, 1e-9);

    // A broken slot is empty, not merely low: the tile drains all the way to
    // the plate colour and refills when the reload hands the pool back.
    rig.damage(rig.petals(0).front(), 6.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.slotHealth(0), 0.0, 1e-9);

    CHECK(rig.tickUntil([&] { return !rig.slot(0).broken; }));
    CHECK_NEAR(rig.slotHealth(0), 1.0, 1e-9);
}

TEST(a_clump_reports_the_share_of_its_grains_still_standing) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "sandy");
    rig.settleEquips();
    CHECK_EQ(rig.petals(0).size(), std::size_t(4));
    CHECK_NEAR(rig.slotHealth(0), 1.0, 1e-9);

    // A grain off the field counts as none of its own health rather than as
    // absent, so three whole grains of four is a tile three quarters full --
    // which is the only reading that makes a half-broken clump legible.
    rig.damage(rig.petalWithSub(0, 0), 12.0);
    rig.tick();
    CHECK_EQ(rig.petals(0).size(), std::size_t(3));
    CHECK_NEAR(rig.slotHealth(0), 0.75, 1e-9);

    // Damage on a surviving grain is its own share of the same total.
    rig.damage(rig.petalWithSub(0, 1), 6.0);
    rig.tick();
    CHECK_NEAR(rig.slotHealth(0), 0.625, 1e-9);
}

TEST(an_unbreakable_petal_never_drains_its_tile) {
    if (!contentLoaded()) return;
    // Rock has no health at all. Reading a pool it does not own would leave its
    // tile permanently empty.
    Rig rig;
    rig.equip(0, "rock");
    rig.settleEquips();
    rig.tick(20);
    CHECK_NEAR(rig.slotHealth(0), 1.0, 1e-9);
}

/// "It comes and goes": gardn's wing is the one petal that does not simply
/// ride the ring out on a swing -- it is thrown a further 120 units past it on
/// a squared sine and pulled back, over and over, for as long as the button is
/// held. Everything about the petal reads as broken without it: the tooltip
/// says nothing else, and a wing that just sits in the ring is a basic.
TEST(a_wing_lunges_past_the_ring_and_comes_back_while_attacking) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "wing");
    // A turning ring makes the spring hold station slightly outside its target
    // point, and this test measures distances against that target. Freezing it
    // takes that steady offset out; the lunge itself is unaffected either way.
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    // At rest it is an ordinary petal. The lunge is gated on the button, not
    // on the petal, so a wing left alone sits on the ring with everything else.
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()), rig.ring().radius, 0.5);
    rig.tick(60);
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()), rig.ring().radius, 0.5);

    rig.setFlags(net::InputAttack);
    // Long enough for the swing out to the extended ring to die away, so
    // every sample below is taken against petals that have stopped moving out
    // and the only thing left travelling is the wing.
    rig.tick(60);
    CHECK_NEAR(rig.ring().radius, kAttackOrbit, 1e-6);

    const double ring = rig.ring().radius;
    std::vector<double> reach;
    for (int i = 0; i < 120; ++i) {
        rig.tick();
        reach.push_back(rig.radiusOf(rig.petals(0).front()));
    }

    // The crest is a full lunge past the extended ring and the trough is back
    // on it. The slack is the spring's: it is pulled toward the moving point
    // rather than pinned to it, so it carries a little past the top and a
    // little inside the bottom.
    const double low = *std::min_element(reach.begin(), reach.end());
    const double high = *std::max_element(reach.begin(), reach.end());
    CHECK_NEAR(high, ring + kWingOrbitLungeReach, 5.0);
    CHECK_NEAR(low, ring, 5.0);

    // And it does it REPEATEDLY, on gardn's period: squaring the sine halves
    // it, so one out-and-back is pi / 2.5 = 1.26s rather than 2.5s.
    std::vector<int> crests;
    for (std::size_t i = 1; i + 1 < reach.size(); ++i) {
        if (reach[i] > reach[i - 1] && reach[i] >= reach[i + 1] && reach[i] > ring + 60.0) {
            crests.push_back(static_cast<int>(i));
        }
    }
    CHECK(crests.size() >= 2);
    const double period = (kPi / kWingOrbitLungeRate) / net::kTickSeconds;
    CHECK_NEAR(period, 37.7, 0.1);
    for (std::size_t i = 1; i < crests.size(); ++i) {
        CHECK_NEAR(static_cast<double>(crests[i] - crests[i - 1]), period, 2.0);
    }

    // Letting go brings it home and leaves it there: the lunge is not a
    // wind-down the petal finishes on its own.
    rig.setFlags(0);
    rig.tick(120);
    CHECK_NEAR(rig.radiusOf(rig.petals(0).front()), rig.ring().radius, 0.5);
}

/// The lunge belongs to the wing alone. Sharing a ring with one must not throw
/// the rest of the loadout around -- it is read per petal, off the config the
/// instance was spawned from.
TEST(only_the_wing_lunges) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "wing");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    rig.setFlags(net::InputAttack);
    rig.tick(60);

    bool wingMoved = false;
    for (int i = 0; i < 120; ++i) {
        rig.tick();
        const double ring = rig.ring().radius;
        if (rig.radiusOf(rig.petals(0).front()) > ring + 60.0) wingMoved = true;
        CHECK_NEAR(rig.radiusOf(rig.petals(1).front()), ring, 0.5);
    }
    CHECK(wingMoved);
}

// ---------------------------------------------------------------------------
// Pearl: shot out onto the ground while attacking
// ---------------------------------------------------------------------------

namespace {

/// How far a pearl may be from its flower: the stock leash, grown by whatever
/// the body has grown, as the pearl's own placement works it out.
double pearlLeash(Rig& rig) {
    return kPearlMaxDistance + rig.world.get<Body>(rig.player).radius - kPlayerBaseRadius;
}

/// A rig holding attack with its pearl already shot out and at rest.
Entity restingPearl(Rig& rig) {
    rig.equip(0, "pearl");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    rig.setFlags(net::InputAttack);
    rig.tick(90);
    return rig.petals(0).front();
}

} // namespace

/// "Shoots out from orbit": on attack the pearl leaves the ring straight out
/// along its bearing, slides to a stop on the ground well past the extended
/// ring, and stays there for as long as the button is held.
TEST(a_pearl_is_shot_out_and_comes_to_rest_on_the_ground_while_attacking) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "pearl");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();

    const Entity pearl = rig.petals(0).front();
    const Entity basic = rig.petals(1).front();
    // At rest it is a petal on the ring like any other.
    const double start = rig.ring().radius;
    CHECK_NEAR(rig.radiusOf(pearl), start, 0.5);
    CHECK(!rig.world.get<PetalInstance>(pearl).thrown);
    const double bearing = rig.angleOf(pearl);

    rig.setFlags(net::InputAttack);
    double previous = rig.radiusOf(pearl);
    for (int i = 0; i < 90; ++i) {
        rig.tick();
        CHECK(rig.world.get<PetalInstance>(pearl).thrown);
        // Straight out and only ever out, and never past the leash.
        const double reach = rig.radiusOf(pearl);
        CHECK(reach >= previous - 1e-9);
        CHECK(reach <= pearlLeash(rig) + 1e-9);
        CHECK(angularGap(rig.angleOf(pearl), bearing) < 0.02);
        previous = reach;
    }
    // It slid its authored distance, which puts it well past the extended ring.
    CHECK_NEAR(previous, start + kPearlLaunchSpeed / kPearlGroundFriction, 1.5);
    CHECK(previous > kAttackOrbit + 80.0);
    CHECK(rig.world.get<PetalInstance>(pearl).flightVelocity.lengthSq() == 0.0);

    // At rest means at rest: not creeping, not drifting back.
    const Vec2 landed = rig.position(pearl);
    rig.tick(60);
    CHECK_NEAR(rig.position(pearl).x, landed.x, 1e-9);
    CHECK_NEAR(rig.position(pearl).y, landed.y, 1e-9);
    // The rest of the ring does what it always did.
    CHECK_NEAR(rig.radiusOf(basic), rig.ring().radius, 0.5);

    // Letting go hands it back to the ring -- once the ring has drawn back in
    // to rest, a few ticks later -- and leaves it there.
    rig.setFlags(0);
    const bool home = rig.tickUntil(
        [&] { return !rig.world.get<PetalInstance>(pearl).thrown; }, 10);
    CHECK(home);
    CHECK_NEAR(rig.ring().gap, kPetalOrbitRestGap, 1e-9);
    rig.tick(120);
    CHECK_NEAR(rig.radiusOf(pearl), rig.ring().radius, 0.5);
}

/// On the ground, not on the flower: a flower moving about inside the leash
/// leaves its pearl exactly where it came to rest.
TEST(a_pearl_stays_where_it_landed_while_its_flower_moves_inside_the_leash) {
    if (!contentLoaded()) return;
    Rig rig;
    const Entity pearl = restingPearl(rig);
    const Vec2 landed = rig.position(pearl);
    const Vec2 toward = (landed - rig.position(rig.player)).normalized();
    const Vec2 across{-toward.y, toward.x};

    for (int i = 0; i < 40; ++i) {
        // Toward it, then sideways: never far enough to pull on the leash.
        rig.world.get<Transform>(rig.player).position += (i < 20 ? toward : across) * 5.0;
        rig.tick();
        CHECK_NEAR(rig.position(pearl).x, landed.x, 1e-9);
        CHECK_NEAR(rig.position(pearl).y, landed.y, 1e-9);
        CHECK(rig.world.get<PetalInstance>(pearl).thrown);
    }
}

/// The leash: a flower walking away drags its pearl along at the maximum
/// distance rather than leaving it behind.
TEST(a_pearl_is_dragged_along_at_its_leash_by_a_flower_walking_away) {
    if (!contentLoaded()) return;
    Rig rig;
    const Entity pearl = restingPearl(rig);
    const Vec2 landed = rig.position(pearl);
    const Vec2 away = (rig.position(rig.player) - landed).normalized();

    // Far enough to go a good way past the leash, whatever it is set to.
    const int ticks = static_cast<int>(std::ceil((pearlLeash(rig) + 300.0) / 12.0));
    for (int i = 0; i < ticks; ++i) {
        rig.world.get<Transform>(rig.player).position += away * 12.0;
        rig.tick();
        CHECK(rig.radiusOf(pearl) <= pearlLeash(rig) + 1e-6);
    }
    // Taut, trailing straight behind, and carried most of the way.
    CHECK_NEAR(rig.radiusOf(pearl), pearlLeash(rig), 1e-6);
    CHECK(angularGap(rig.angleOf(pearl), (-away).angle()) < 1e-6);
    CHECK((rig.position(pearl) - landed).length() > 500.0);

    // Dragged, it still lies where the leash left it once the flower stops.
    const Vec2 stopped = rig.position(pearl);
    rig.tick(30);
    CHECK_NEAR(rig.position(pearl).x, stopped.x, 1e-9);
    CHECK_NEAR(rig.position(pearl).y, stopped.y, 1e-9);
}

/// The pearl goes out and comes in with the ring's EXTENSION, not with the
/// buttons. Attack and defend held together is an extended ring -- attack wins
/// -- so the pearl stays out; the ring drawing in brings it home on the tick
/// the extension is back at rest, which -- the ring switching at once, as
/// gardn's does -- is the tick attack is let go.
TEST(a_pearl_follows_the_rings_extension_not_the_buttons) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "pearl");
    rig.freezeRing();
    rig.settleEquips();
    rig.settleRing();
    const Entity pearl = rig.petals(0).front();

    // Out on the first tick the ring reaches past its rest radius.
    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK(rig.ring().gap > kPetalOrbitRestGap);
    CHECK(rig.world.get<PetalInstance>(pearl).thrown);
    rig.tick(9);
    CHECK(rig.radiusOf(pearl) > kAttackOrbit);

    // Defend on top of attack leaves the ring extended, and the pearl with it.
    rig.setFlags(net::InputAttack | net::InputDefend);
    rig.tick(30);
    CHECK_NEAR(rig.ring().gap, kPetalOrbitAttackGap, 1e-9);
    CHECK(rig.world.get<PetalInstance>(pearl).thrown);

    // Defend alone draws the ring in, and the pearl comes home with it.
    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK_NEAR(rig.ring().gap, kPetalOrbitDefendGap, 1e-9);
    CHECK(!rig.world.get<PetalInstance>(pearl).thrown);
    rig.tick(120);
    CHECK(!rig.world.get<PetalInstance>(pearl).thrown);
    CHECK_NEAR(rig.radiusOf(pearl), rig.ring().radius, 0.5);

    // Extending again throws it again.
    rig.setFlags(net::InputAttack);
    rig.tick();
    CHECK(rig.world.get<PetalInstance>(pearl).thrown);
}

// ---------------------------------------------------------------------------
// Bone and cotton, as the ring spawns them
// ---------------------------------------------------------------------------

TEST(a_spawned_bone_wears_its_armor_and_a_spawned_cotton_soaks) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "bone", Rarity::Rare);
    rig.equip(1, "cotton");
    rig.equip(2, "basic");
    rig.settleEquips();

    const Entity bone = rig.petals(0).front();
    const Entity cotton = rig.petals(1).front();
    const Entity basic = rig.petals(2).front();
    // 10 at common, up the mob armour ladder to rare.
    CHECK(rig.world.has<Armor>(bone));
    CHECK_NEAR(rig.world.get<Armor>(bone).amount, 10.0 * kMobArmorScale[2], 1e-9);
    CHECK(!rig.world.has<Armor>(basic));
    CHECK(!rig.world.has<Armor>(cotton));

    CHECK(rig.world.get<PetalInstance>(cotton).soaksOwnerDamage);
    CHECK(!rig.world.get<PetalInstance>(bone).soaksOwnerDamage);
    CHECK(!rig.world.get<PetalInstance>(basic).soaksOwnerDamage);
}

// ---------------------------------------------------------------------------
// Loose petals (wax)
// ---------------------------------------------------------------------------

namespace {

/// Ticks enough to serve wax's thirty-second equip reload, which is longer
/// than settleEquips waits by default.
constexpr int kWaxEquipTicks = 1000;

/// The flower's one loose petal, or NULL_ENTITY.
Entity loosePetalOf(Rig& rig) {
    for (const Entity e : rig.petals()) {
        if (rig.world.has<LoosePetal>(e)) return e;
    }
    return NULL_ENTITY;
}

} // namespace

TEST(wax_is_thirty_units_across_at_common_and_thirty_more_every_tier) {
    if (!contentLoaded()) return;
    const std::uint16_t wax = petalId("wax");
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        const PetalStats stats =
            fixture().registry.petalStats(wax, static_cast<Rarity>(tier));
        CHECK_NEAR(stats.radius, 30.0 * (tier + 1), 1e-12);
    }
    CHECK_NEAR(fixture().registry.petalStats(wax, Rarity::Apex).radius, 300.0, 1e-12);
    // Universal is apex-sized: only its health and damage are above apex.
    CHECK_NEAR(fixture().registry.petalStats(wax, Rarity::Universal).radius, 300.0, 1e-12);
    // Every other petal keeps the radius its size gives it.
    CHECK_NEAR(fixture().registry.petalStats(petalId("basic"), Rarity::Apex).radius, 20.0, 1e-12);
    CHECK(petalIsLooseBody(fixture().registry.petal(wax)));
    CHECK(!petalIsLooseBody(fixture().registry.petal(petalId("pearl"))));

    // The body it is spawned with is that radius -- and a quarter of a
    // flower's mass, the same at every tier, so a big slab is no harder to
    // push than a small one.
    Rig rig;
    rig.equip(0, "wax", Rarity::Legendary);
    rig.settleEquips(kWaxEquipTicks);
    const Entity slab = loosePetalOf(rig);
    CHECK(slab != NULL_ENTITY);
    if (slab == NULL_ENTITY) return;
    CHECK_NEAR(rig.world.get<Body>(slab).radius, 150.0, 1e-12);
    CHECK_NEAR(rig.world.get<Body>(slab).mass, kLoosePetalMass, 1e-12);
    CHECK_NEAR(kLoosePetalMass, 0.25, 1e-12);
    CHECK(rig.world.get<Replicated>(slab).spawnFlags & net::SpawnLoosePetal);
    CHECK(!(rig.world.get<Replicated>(slab).spawnFlags & net::SpawnRingPetal));
}

TEST(wax_is_put_down_behind_the_flower_and_takes_no_place_on_the_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "wax");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips(kWaxEquipTicks);
    rig.settleRing();

    // Behind the flower's heading (east, at angle 0), clear of its body and
    // its own by the gap.
    const Entity slab = loosePetalOf(rig);
    CHECK(slab != NULL_ENTITY);
    if (slab == NULL_ENTITY) return;
    const Vec2 flower = rig.position(rig.player);
    CHECK_NEAR(rig.position(slab).x,
               flower.x - (kPlayerBaseRadius + 30.0 + kLoosePetalSpawnGap), 1e-9);
    CHECK_NEAR(rig.position(slab).y, flower.y, 1e-9);

    // The basic and the anchor split the ring between them, as if the wax
    // were not equipped at all: half a turn apart, on the ring's radius.
    const Entity basic = rig.petals(1).front();
    const Entity anchor = rig.petals(Rig::kAnchorSlot).front();
    CHECK_NEAR(angularGap(rig.angleOf(basic), rig.angleOf(anchor)), kPi, 1e-6);
    CHECK_NEAR(rig.radiusOf(basic), rig.ring().radius, 1e-6);

    // Attacking throws the ring out and leaves the slab where it lies; no
    // spring, no orbit, no attraction.
    const Vec2 laid = rig.position(slab);
    rig.setFlags(net::InputAttack);
    rig.tick(30);
    CHECK_NEAR(rig.position(slab).x, laid.x, 1e-12);
    CHECK_NEAR(rig.position(slab).y, laid.y, 1e-12);

    // And the flower walking away from it leaves it standing there.
    rig.setFlags(0);
    rig.withMovement();
    rig.setMove(0.0);
    rig.tick(20);
    CHECK(rig.position(rig.player).x > flower.x + 100.0);
    CHECK_NEAR(rig.position(slab).x, laid.x, 1e-12);
    CHECK_NEAR(rig.position(slab).y, laid.y, 1e-12);
}

TEST(a_wax_stays_however_far_its_flower_goes_and_follows_it_across_realms) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "wax");
    rig.settleEquips(kWaxEquipTicks);
    const Entity slab = loosePetalOf(rig);
    CHECK(slab != NULL_ENTITY);
    if (slab == NULL_ENTITY) return;

    // Far off the screen -- a whole map away -- it is neither taken back nor
    // moved, and the slot does not reload: it is still out there.
    const Vec2 laid = rig.position(slab);
    rig.world.get<Transform>(rig.player).position = laid + Vec2{20000.0, 15000.0};
    rig.tick(50);
    CHECK(rig.world.isAlive(slab));
    CHECK_NEAR(rig.position(slab).x, laid.x, 1e-12);
    CHECK_NEAR(rig.position(slab).y, laid.y, 1e-12);
    CHECK(!rig.slot(0).broken);

    // Stranded in another realm from its flower -- which is what a teleporter
    // or joining the maze leaves it -- it is the SAME slab put down behind the
    // flower on its side, with no reload.
    Transform& flower = rig.world.get<Transform>(rig.player);
    flower.realm = Realm::Maze;
    flower.angle = kPi * 0.5;
    const Vec2 arrived = flower.position;
    rig.tick();
    CHECK(rig.world.isAlive(slab));
    CHECK(!rig.slot(0).broken);
    CHECK(rig.world.get<Transform>(slab).realm == Realm::Maze);
    CHECK_NEAR(rig.position(slab).x, arrived.x, 1e-9);
    CHECK_NEAR(rig.position(slab).y,
               arrived.y - (kPlayerBaseRadius + 30.0 + kLoosePetalSpawnGap), 1e-9);
}

TEST(each_wax_lands_at_its_own_random_rotation_and_keeps_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "wax");
    rig.equip(1, "wax");
    rig.settleEquips(kWaxEquipTicks);
    std::vector<Entity> slabs;
    for (const Entity e : rig.petals()) {
        if (rig.world.has<LoosePetal>(e)) slabs.push_back(e);
    }
    CHECK_EQ(slabs.size(), std::size_t(2));
    if (slabs.size() != 2) return;

    std::vector<double> angles;
    for (const Entity slab : slabs) {
        const double angle = rig.world.get<Transform>(slab).angle;
        // What the client is sent to draw it at is the instance's facing.
        CHECK_NEAR(rig.world.get<PetalInstance>(slab).facingAngle, angle, 1e-12);
        CHECK(angle >= 0.0 && angle < kTau);
        angles.push_back(angle);
    }
    // Two rolls, not one angle for every slab -- and not the 0 every other
    // petal is born at.
    CHECK(std::fabs(angles[0] - angles[1]) > 1e-6);
    CHECK(std::fabs(angles[0]) > 1e-6 && std::fabs(angles[1]) > 1e-6);

    // Kept: nothing turns a slab once it is down, attacking or not.
    rig.setFlags(net::InputAttack);
    rig.tick(30);
    for (std::size_t i = 0; i < slabs.size(); ++i) {
        CHECK_NEAR(rig.world.get<Transform>(slabs[i]).angle, angles[i], 1e-12);
        CHECK_NEAR(rig.world.get<PetalInstance>(slabs[i]).facingAngle, angles[i], 1e-12);
    }
}

// ---------------------------------------------------------------------------
// The moon: a loose body the ring orbits
// ---------------------------------------------------------------------------

namespace {

/// Ticks enough to serve the moon's ten-second equip reload.
constexpr int kMoonEquipTicks = 400;

/// The flower's moon on the field, or NULL_ENTITY.
Entity moonOf(Rig& rig) {
    for (const Entity e : rig.petals()) {
        if (rig.world.has<RingAnchor>(e)) return e;
    }
    return NULL_ENTITY;
}

} // namespace

TEST(a_moon_is_a_loose_body_thirty_units_a_tier_like_wax) {
    if (!contentLoaded()) return;
    const std::uint16_t moon = petalId("moon");
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        CHECK_NEAR(fixture().registry.petalStats(moon, static_cast<Rarity>(tier)).radius,
                   30.0 * (tier + 1), 1e-12);
    }
    CHECK(petalIsLooseBody(fixture().registry.petal(moon)));
    CHECK(petalAnchorsRing(fixture().registry.petal(moon)));
    // Wax is loose and anchors nothing.
    CHECK(!petalAnchorsRing(fixture().registry.petal(petalId("wax"))));

    Rig rig;
    rig.equip(0, "moon", Rarity::Legendary);
    rig.settleEquips(kMoonEquipTicks);
    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;
    CHECK(rig.world.has<LoosePetal>(body));
    CHECK_NEAR(rig.world.get<Body>(body).radius, 150.0, 1e-12);
    CHECK_NEAR(rig.world.get<Body>(body).mass, kLoosePetalMass, 1e-12);
    // The one petal with momentum, for a bubble to throw.
    CHECK(rig.world.has<Motion>(body));
    CHECK(rig.world.get<Replicated>(body).spawnFlags & net::SpawnLoosePetal);
    // Put down behind the flower, clear of it, as wax is.
    const Vec2 flower = rig.position(rig.player);
    CHECK_NEAR(rig.position(body).x,
               flower.x - (kPlayerBaseRadius + 150.0 + kLoosePetalSpawnGap), 1e-9);
    CHECK_NEAR(rig.position(body).y, flower.y, 1e-9);
}

TEST(the_ring_orbits_the_moon_as_it_would_a_flower_that_size) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "moon");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips(kMoonEquipTicks);
    rig.settleRing();

    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;
    const Vec2 centre = rig.position(body);

    // The rest radius off the MOON's edge: 60 for a 25-unit flower, so 65
    // around a 30-unit moon.
    CHECK_NEAR(rig.ring().radius, 30.0 + kPetalOrbitRestGap, 1e-9);
    const Entity basic = rig.petals(1).front();
    const Entity anchor = rig.petals(Rig::kAnchorSlot).front();
    CHECK_NEAR(distance(rig.position(basic), centre), rig.ring().radius, 1e-3);
    CHECK_NEAR(distance(rig.position(anchor), centre), rig.ring().radius, 1e-3);
    // The moon takes no place on its own ring: the basic and the anchor split
    // it half and half, about the moon.
    CHECK_NEAR(angularGap((rig.position(basic) - centre).angle(),
                          (rig.position(anchor) - centre).angle()),
               kPi, 1e-3);

    // Attacking throws the ring out around the moon, which stays put.
    rig.setFlags(net::InputAttack);
    rig.tick(60);
    CHECK_NEAR(rig.position(body).x, centre.x, 1e-12);
    CHECK_NEAR(rig.position(body).y, centre.y, 1e-12);
    CHECK(distance(rig.position(basic), centre) > 65.0 * 1.5);

    // And the flower walking off leaves the moon, and the ring with it.
    rig.setFlags(0);
    rig.withMovement();
    rig.setMove(0.0);
    rig.tick(30);
    CHECK(rig.position(rig.player).x > centre.x + 300.0);
    CHECK_NEAR(distance(rig.position(basic), centre), rig.ring().radius, 2.0);
}

TEST(the_ring_flies_back_to_the_flower_when_its_moon_breaks) {
    if (!contentLoaded()) return;
    Rig rig;
    // Facing west, so the moon is put down east of the flower and the basic's
    // place on its ring is further east again, well off the flower.
    rig.world.get<Transform>(rig.player).angle = kPi;
    rig.equip(0, "moon");
    rig.equip(1, "basic");
    rig.freezeRing();
    rig.settleEquips(kMoonEquipTicks);
    rig.settleRing();
    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;
    const Entity basic = rig.petals(1).front();
    CHECK(distance(rig.position(basic), rig.position(rig.player)) > 100.0);

    rig.damage(body, 1e12);
    rig.tick();
    CHECK(moonOf(rig) == NULL_ENTITY);
    CHECK(rig.slot(0).broken);

    // Flown home on the glide, not flung past it on the spring: no tick of the
    // trip carries it further from the flower than it started.
    double gap = distance(rig.position(basic), rig.position(rig.player));
    for (int i = 0; i < 10; ++i) {
        rig.tick();
        const double now = distance(rig.position(basic), rig.position(rig.player));
        CHECK(now <= gap + 1e-9);
        gap = now;
    }
    // Stay inside the reload window: a new Moon must not be mistaken for
    // the old ring failing to return. The current reload talent is faster.
    rig.settleRing(120);
    CHECK(rig.slot(0).broken);
    CHECK_NEAR(rig.ring().radius, kRestOrbit, 1e-9);
    CHECK_NEAR(rig.radiusOf(basic), rig.ring().radius, 1e-3);
}

TEST(a_flower_has_one_moon_however_many_are_equipped) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "moon");
    rig.equip(2, "moon", Rarity::Rare);
    rig.equip(4, "moon", Rarity::Rare);
    rig.settleEquips(kMoonEquipTicks);
    rig.tick(10);

    // The best tier comes out, and the leftmost of the two rares.
    std::vector<Entity> moons;
    for (const Entity e : rig.petals()) {
        if (rig.world.has<LoosePetal>(e)) moons.push_back(e);
    }
    CHECK_EQ(moons.size(), std::size_t(1));
    if (moons.size() != 1) return;
    CHECK_EQ(int(rig.world.get<PetalInstance>(moons.front()).slot), 2);
    CHECK_EQ(liveMoonSlotOf(fixture().registry, rig.world.get<Loadout>(rig.player)), 2);
    // The others sit ready, holding nothing.
    CHECK(!rig.slot(0).broken);
    CHECK(!rig.slot(4).broken);

    // Breaking the live moon does not let another one out.
    rig.damage(moons.front(), 1e12);
    rig.tick(20);
    CHECK(moonOf(rig) == NULL_ENTITY);

    // Taking it off hands the moon to the next best, after its own reload.
    rig.unequip(2);
    rig.tick();
    CHECK_EQ(liveMoonSlotOf(fixture().registry, rig.world.get<Loadout>(rig.player)), 4);
    CHECK(rig.tickUntil([&] { return moonOf(rig) != NULL_ENTITY; }, kMoonEquipTicks));
    const Entity next = moonOf(rig);
    if (next != NULL_ENTITY) {
        CHECK_EQ(int(rig.world.get<PetalInstance>(next).slot), 4);
    }
}

TEST(a_bubble_on_a_moons_ring_throws_the_moon_and_not_the_flower) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.withMovement();
    rig.equip(0, "moon");
    rig.equip(1, "bubble");
    rig.freezeRing();
    rig.settleEquips(kMoonEquipTicks);
    rig.settleRing();
    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;
    const Entity bubble = rig.petals(1).front();
    const Vec2 moonBefore = rig.position(body);
    const Vec2 flowerBefore = rig.position(rig.player);
    // Clear of ITSELF, as a flower is: away from the bubble's side of the ring.
    const Vec2 away = (moonBefore - rig.position(bubble)).normalized();

    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK(rig.velocity(body).length() > 0.0);
    CHECK_NEAR(rig.velocity(rig.player).length(), 0.0, 1e-12);
    rig.setFlags(0);
    rig.tick(90);

    // The flower's reach, carried by the moon instead; the flower never moved.
    const Vec2 thrown = rig.position(body) - moonBefore;
    CHECK_NEAR(thrown.length(), kBubblePopDistance, 1.0);
    CHECK_NEAR(thrown.normalized().x * away.x + thrown.normalized().y * away.y, 1.0, 1e-6);
    CHECK_NEAR(rig.velocity(body).length(), 0.0, 0.01);
    CHECK_NEAR(distance(rig.position(rig.player), flowerBefore), 0.0, 1e-9);
}

TEST(a_heal_on_a_moons_ring_dives_to_the_flower) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "moon");
    rig.equip(1, "healer");
    rig.settleEquips(kMoonEquipTicks);
    rig.tick(40);   // past the healer's charge, still orbiting a full flower
    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;

    // Missing health calls it home -- to the flower, not to the moon its ring
    // is laid out around -- and it lands by touch, well inside the window an
    // undelivered dive is paid out at.
    rig.world.get<Health>(rig.player).current = 50.0;
    const int window = static_cast<int>(kPetalHomingTimeoutMillis / net::kTickMillis);
    const bool delivered = rig.tickUntil([&] { return rig.slot(1).broken; }, window * 2 / 3);
    CHECK(delivered);
    // The burst's 10, and a few ticks of regeneration.
    CHECK(rig.world.get<Health>(rig.player).current >= 60.0);
}

TEST(a_moon_carried_to_another_realm_lays_its_ring_out_where_it_lands) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "moon");
    rig.equip(1, "basic");
    rig.settleEquips(kMoonEquipTicks);
    const Entity body = moonOf(rig);
    CHECK(body != NULL_ENTITY);
    if (body == NULL_ENTITY) return;

    // A teleporter takes the flower to another realm; the moon is carried
    // after it, the same body put down behind it, and the ring's centre is
    // the moon's new place on that same tick -- not the coordinates it left
    // in the old realm.
    Transform& flower = rig.world.get<Transform>(rig.player);
    flower.realm = Realm::Maze;
    flower.position += Vec2{4000.0, 0.0};
    // The server puts the rest of the kit on the flower as it moves it
    // (GameServer::moveEntityToRealm); the moon it leaves to this system.
    const Entity basic = rig.petals(1).front();
    rig.world.get<Transform>(basic).realm = Realm::Maze;
    rig.world.get<Transform>(basic).position = flower.position;
    rig.tick();
    CHECK(rig.world.isAlive(body));
    CHECK(rig.world.get<Transform>(body).realm == Realm::Maze);
    CHECK(distance(rig.position(body), rig.position(rig.player)) < 100.0);
    const PetalSlotState& state = rig.world.get<PetalSlotState>(rig.player);
    CHECK(state.ringAnchor == body);
    CHECK_NEAR(distance(state.ringCentre, rig.position(body)), 0.0, 1e-9);
    // Sprung toward a place on the moon's new ring, a step from the flower --
    // not hurled at a centre four thousand units back in the old realm.
    CHECK(distance(rig.position(basic), rig.position(rig.player)) < 200.0);
}

// ---------------------------------------------------------------------------
// Mimic: the petal to its left, at its own tier, on the left petal's reload
// ---------------------------------------------------------------------------

namespace {

const PetalInstance& instanceOf(Rig& rig, Entity petal) {
    return rig.world.get<PetalInstance>(petal);
}

} // namespace

TEST(a_mimic_fields_the_petal_to_its_left_at_its_own_tier) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic", Rarity::Common);
    rig.equip(1, "mimic", Rarity::Rare);
    rig.settleEquips();

    const std::vector<Entity> copies = rig.petals(1);
    CHECK_EQ(copies.size(), std::size_t(1));
    if (copies.empty()) return;
    const PetalInstance& copy = instanceOf(rig, copies[0]);
    CHECK_EQ(copy.configIndex, petalId("basic"));
    CHECK(copy.rarity == Rarity::Rare);
    // Drawn as the petal it copies, at the mimic's tier.
    CHECK_EQ(rig.world.get<Replicated>(copies[0]).typeIndex, petalId("basic"));
    CHECK(rig.world.get<Replicated>(copies[0]).rarity == Rarity::Rare);
    // A rare basic's body, not the mimic's own zero-health one.
    const PetalStats rare = fixture().registry.petalStats(petalId("basic"), Rarity::Rare);
    CHECK_NEAR(rig.healthOf(copies[0]), rare.health, 1e-9);
    CHECK_NEAR(rig.world.get<ContactDamage>(copies[0]).amount, rare.damage, 1e-9);
    // And the left slot is untouched by it.
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    CHECK(instanceOf(rig, rig.petals(0)[0]).rarity == Rarity::Common);
}

TEST(a_mimic_reloads_on_the_left_petals_reload_not_its_own) {
    if (!contentLoaded()) return;
    Rig rig;
    // Bubble is a petal whose reload shortens with tier, so it tells the left
    // slot's reload apart from the mimic tier's -- and both from the mimic's
    // own ten seconds.
    rig.equip(0, "bubble", Rarity::Common);
    rig.equip(1, "mimic", Rarity::Legendary);
    const double equippedAt = rig.now + net::kTickMillis;
    rig.tick();

    const double leftReload =
        fixture().registry.petalStats(petalId("bubble"), Rarity::Common).reloadMillis;
    const double ownTierReload =
        fixture().registry.petalStats(petalId("bubble"), Rarity::Legendary).reloadMillis;
    CHECK(ownTierReload < leftReload);
    CHECK(rig.slot(1).broken);
    CHECK_NEAR(rig.slot(1).reloadReadyAtMillis, equippedAt + std::max(leftReload*kPetalReloadFeelScale,SkillSet{}.equipReloadFloorMillis()), 1e-9);

    // Broken in the field, it pays the same one again.
    rig.settleEquips();
    const std::vector<Entity> copies = rig.petals(1);
    CHECK_EQ(copies.size(), std::size_t(1));
    if (copies.empty()) return;
    rig.damage(copies[0], 1000.0);
    const double brokeAt = rig.now + net::kTickMillis;
    rig.tick();
    CHECK(rig.slot(1).broken);
    CHECK_NEAR(rig.slot(1).reloadReadyAtMillis, brokeAt + leftReload*kPetalReloadFeelScale, 1e-9);
}

TEST(a_mimic_with_nothing_to_its_left_is_a_mimic) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "mimic");       // the row's first slot
    rig.equip(3, "mimic");       // slot 2 is empty
    const double equippedAt = rig.now + net::kTickMillis;
    rig.tick();
    const double ownReload =
        fixture().registry.petalStats(petalId("mimic"), Rarity::Common).reloadMillis;
    for (const int i : {0, 3}) {
        CHECK_EQ(rig.world.get<PetalSlotState>(rig.player).slots[static_cast<std::size_t>(i)]
                     .configIndex,
                 petalId("mimic"));
        CHECK_NEAR(rig.slot(i).reloadReadyAtMillis, equippedAt + std::max(ownReload*kPetalReloadFeelScale,SkillSet{}.equipReloadFloorMillis()), 1e-9);
    }
    rig.settleEquips(1000);
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    if (!rig.petals(0).empty()) {
        CHECK_EQ(instanceOf(rig, rig.petals(0)[0]).configIndex, petalId("mimic"));
    }
}

TEST(a_mimic_follows_its_neighbour_when_the_neighbour_changes) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic");
    rig.equip(1, "mimic", Rarity::Epic);
    rig.settleEquips();
    CHECK_EQ(rig.petals(1).size(), std::size_t(1));

    // A clump to its left makes the mimic a clump, grain for grain, and it
    // serves the clump's reload before any of them appear.
    rig.equip(0, "sandy");
    rig.tick();
    CHECK(rig.petals(1).empty());
    rig.settleEquips();
    const std::vector<Entity> grains = rig.petals(1);
    CHECK_EQ(grains.size(), std::size_t(4));
    for (const Entity grain : grains) {
        CHECK_EQ(instanceOf(rig, grain).configIndex, petalId("sandy"));
        CHECK(instanceOf(rig, grain).rarity == Rarity::Epic);
    }

    // And emptying the slot to its left hands it back its own stats.
    rig.unequip(0);
    rig.tick();
    CHECK(rig.petals(1).empty());
    CHECK_EQ(rig.world.get<PetalSlotState>(rig.player).slots[1].configIndex, petalId("mimic"));
}

TEST(a_run_of_mimics_copies_the_first_real_petal_each_at_its_own_tier) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basic", Rarity::Common);
    rig.equip(1, "mimic", Rarity::Rare);
    rig.equip(2, "mimic", Rarity::Epic);
    rig.settleEquips();
    for (const auto& [slot, tier] : {std::pair{1, Rarity::Rare}, std::pair{2, Rarity::Epic}}) {
        const std::vector<Entity> copies = rig.petals(slot);
        CHECK_EQ(copies.size(), std::size_t(1));
        if (copies.empty()) continue;
        CHECK_EQ(instanceOf(rig, copies[0]).configIndex, petalId("basic"));
        CHECK(instanceOf(rig, copies[0]).rarity == tier);
    }
}

TEST(sawblade_reloads_in_one_second_after_breaking_and_hits_harder_from_eternal) {
    const auto& registry = testsupport::shippedContent();
    const auto sawblade = registry.petalIndex("sawblade");
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        const auto rarity = static_cast<Rarity>(tier);
        CHECK_NEAR(registry.petalStats(sawblade, rarity).reloadMillis, 2000.0, 1e-9);
    }
    constexpr double combatReloadMillis = 1000.0;
    for (const auto rarity : {Rarity::Super, Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        Rig rig(registry);
        rig.equip(0, "sawblade", rarity);
        rig.settleEquips();
        const auto petals = rig.petals(0);
        CHECK_EQ(petals.size(), std::size_t(1));
        if (petals.empty()) continue;
        const double contactDamage = rig.world.get<ContactDamage>(petals.front()).amount;
        if (rarity == Rarity::Super) {
            CHECK_NEAR(contactDamage, 87480.0, 1e-6);
        } else {
            const double expected = rarity == Rarity::Unique ? 442868.0 : 885736.0;
            CHECK(contactDamage > registry.petalStats(registry.petalIndex("magic_peas"), rarity).lightningDamagePerHit);
            CHECK_NEAR(contactDamage, expected, 1e-6);
        }
        rig.damage(petals.front(), rig.healthOf(petals.front()));
        const double brokenAt = rig.now + net::kTickMillis;
        rig.tick();
        CHECK_NEAR(rig.slot(0).reloadReadyAtMillis, brokenAt + combatReloadMillis, 1e-9);
        rig.tick(static_cast<int>(combatReloadMillis / net::kTickMillis) - 1);
        CHECK(rig.petals(0).empty());
        rig.tick();
        CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    }
}

TEST(a_mimic_beside_ram_fields_a_sawblade) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "ram", Rarity::Common);
    rig.equip(1, "mimic", Rarity::Rare);
    rig.equip(2, "mimic", Rarity::Epic);
    const double equippedAt = rig.now + net::kTickMillis;
    rig.tick();
    // On the sawblade's reload at RAM's tier.
    CHECK_NEAR(rig.slot(1).reloadReadyAtMillis,
               equippedAt +
                   fixture().registry.petalStats(petalId("sawblade"), Rarity::Common).reloadMillis,
               1e-9);

    rig.settleEquips();
    // A run of mimics reaches the RAM too, and each comes out a sawblade.
    for (const auto& [slot, tier] : {std::pair{1, Rarity::Rare}, std::pair{2, Rarity::Epic}}) {
        const std::vector<Entity> copies = rig.petals(slot);
        CHECK_EQ(copies.size(), std::size_t(1));
        if (copies.empty()) continue;
        CHECK_EQ(instanceOf(rig, copies[0]).configIndex, petalId("sawblade"));
        CHECK(instanceOf(rig, copies[0]).rarity == tier);
        CHECK_EQ(rig.world.get<Replicated>(copies[0]).typeIndex, petalId("sawblade"));
    }
    // The RAM itself stays a RAM.
    CHECK_EQ(rig.petals(0).size(), std::size_t(1));
    if (!rig.petals(0).empty()) {
        CHECK_EQ(instanceOf(rig, rig.petals(0)[0]).configIndex, petalId("ram"));
    }
}

TEST(a_mimic_grants_the_modifiers_of_the_petal_it_copies) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "lucky");
    rig.tick();
    const double one = rig.modifiers().luck;
    rig.equip(1, "mimic");
    rig.tick();
    // Worn modifiers count from the moment of equipping, reload or not.
    CHECK_NEAR(rig.modifiers().luck - 1.0, 2.0 * (one - 1.0), 1e-9);
}

TEST(a_mimic_never_reaches_back_into_the_row_above) {
    if (!contentLoaded()) return;
    Loadout loadout;
    loadout.slots[kLoadoutActiveSlots - 1] = LoadoutSlot{petalId("basic"), Rarity::Common};
    loadout.slots[kLoadoutActiveSlots] = LoadoutSlot{petalId("mimic"), Rarity::Rare};
    const EquippedPetal resolved =
        equippedPetal(fixture().registry, loadout, kLoadoutActiveSlots);
    CHECK_EQ(resolved.configIndex, petalId("mimic"));
    CHECK(resolved.rarity == Rarity::Rare);
    CHECK(resolved.reloadRarity == Rarity::Rare);
}

// ---------------------------------------------------------------------------
// The termite petals
// ---------------------------------------------------------------------------

namespace {

Entity wildMob(Rig& rig, Vec2 at, Rarity rarity) {
    const Entity mob = rig.world.create();
    rig.world.add<MobTag>(mob);
    rig.world.add<MobType>(mob, MobType{fixture().registry.mobIndex("brute"), rarity, 1.0});
    rig.world.add<Transform>(mob, Transform{at, 0.0});
    rig.world.add<Body>(mob, Body{20.0, 1.0});
    rig.world.add<Health>(mob, Health{500.0, 500.0, 0.0, 0.0});
    rig.world.add<Faction>(mob, Faction{Team::Hostiles, false});
    return mob;
}

/// How far `angle` is from the bearing `from` -> `to`.
double offBearing(double angle, Vec2 from, Vec2 to) {
    return std::abs(wrapAngle(angle - (to - from).angle()));
}

} // namespace

TEST(a_compass_points_at_the_best_mob_in_its_realm_until_a_magnet_turns_it) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.freezeRing();
    rig.equip(0, "compass");
    rig.settleEquips();
    // A common mob close by to the south, an epic one far to the east, and a
    // mythic in another realm: the epic is the best the compass can reach.
    wildMob(rig, {1000.0, 1300.0}, Rarity::Common);
    const Entity epic = wildMob(rig, {4000.0, 1000.0}, Rarity::Epic);
    const Entity elsewhere = wildMob(rig, {1000.0, -2000.0}, Rarity::Mythic);
    rig.world.get<Transform>(elsewhere).realm = Realm::Arena;
    rig.tick(3);

    const auto compass = rig.petals(0);
    CHECK_EQ(compass.size(), std::size_t(1));
    if (compass.empty()) return;
    const Vec2 at = rig.world.get<Transform>(compass[0]).position;
    const double facing = rig.world.get<PetalInstance>(compass[0]).facingAngle;
    // Within florr's 0.1 rad wobble, and the tick of travel since it aimed.
    CHECK(offBearing(facing, at, rig.world.get<Transform>(epic).position) < 0.12);

    // A magnet on the same ring drags the needle round to itself.
    rig.equip(1, "magnet");
    rig.settleEquips();
    rig.tick();
    const auto magnets = rig.petals(1);
    CHECK_EQ(magnets.size(), std::size_t(1));
    if (magnets.empty()) return;
    const Vec2 needle = rig.world.get<Transform>(compass[0]).position;
    CHECK(offBearing(rig.world.get<PetalInstance>(compass[0]).facingAngle, needle,
                     rig.world.get<Transform>(magnets[0]).position) < 0.25);
}

TEST(a_relic_raises_max_health_by_a_tenth_a_tier_and_marks_the_flower_a_sharer) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.tick();
    CHECK(!rig.modifiers().sharesDamage);
    rig.equip(0, "relic", Rarity::Rare);
    rig.tick();
    CHECK_NEAR(rig.modifiers().maxHealthScale, 1.3, 1e-9);
    CHECK(rig.modifiers().sharesDamage);
    CHECK_NEAR(fixture().registry.petalStats(petalId("relic"), Rarity::Ultra).modifiers.maxHealth,
               1.7, 1e-9);
}

TEST(a_worn_cursor_hands_its_best_tier_to_the_pet_controls) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.tick();
    CHECK(!rig.modifiers().controlsPets);
    rig.equip(0, "cursor", Rarity::Common);
    rig.equip(1, "cursor", Rarity::Epic);
    rig.tick();
    CHECK(rig.modifiers().controlsPets);
    CHECK_EQ(rig.modifiers().petControlRarity, Rarity::Epic);
    rig.equip(1, "rubber", Rarity::Epic);
    rig.tick();
    CHECK(rig.modifiers().controlsPets);
    CHECK_EQ(rig.modifiers().petControlRarity, Rarity::Common);
}

TEST(rubber_adds_knockback_per_tier_and_soaks_lightning) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "rubber", Rarity::Epic);
    rig.equip(1, "rubber", Rarity::Common);
    rig.settleEquips();
    // +1.2 at epic, +0.3 at common, summed.
    CHECK_NEAR(rig.modifiers().knockbackTakenScale, 1.0 + 1.2 + 0.3, 1e-9);
    const auto rubber = rig.petals(0);
    CHECK_EQ(rubber.size(), std::size_t(1));
    if (!rubber.empty()) {
        CHECK(rig.world.get<PetalInstance>(rubber[0]).soaksOwnerLightning);
        CHECK(!rig.world.get<PetalInstance>(rubber[0]).soaksOwnerDamage);
    }
}

TEST(an_amulet_turns_its_tiers_share_of_overheal_into_shield) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "amulet", Rarity::Epic);
    rig.equip(1, "amulet", Rarity::Common);
    rig.settleEquips();
    // +5% a tier, florr's figure: 20% at epic. The better one, not the sum.
    CHECK_NEAR(rig.modifiers().overhealShield, 0.2, 1e-9);
    CHECK_NEAR(fixture().registry.petalStats(petalId("amulet"), Rarity::Ultra).overhealShield,
               0.35, 1e-9);

    Health& health = rig.world.get<Health>(rig.player);
    ShieldState& shield = rig.world.get<ShieldState>(rig.player);
    shield.amount = 0.0;
    health.current = health.max - 10.0;
    // Ten fill the bar; the amulet keeps a fifth of the spill.
    CombatSystem::landHeal(rig.world, rig.player, health, 60.0);
    CHECK_NEAR(health.current, health.max, 1e-9);
    CHECK_NEAR(shield.amount, (60.0 * kPlayerHealMultiplier - 10.0) * 0.2, 1e-9);
    // Capped at max health, as a shell's shield is.
    CombatSystem::landHeal(rig.world, rig.player, health, health.max * 100.0);
    CHECK_NEAR(shield.amount, health.max, 1e-9);
}

TEST(a_basil_grows_every_heal_by_florrs_bonus_and_stacks_harmonically) {
    if (!contentLoaded()) return;
    // florr's Heal Bonus, 0.2 + 0.05 a tier: 45% at mythic, 50% at ultra.
    CHECK_NEAR(fixture().registry.petalStats(petalId("basil"), Rarity::Mythic).healBonus, 0.45,
               1e-9);
    CHECK_NEAR(fixture().registry.petalStats(petalId("basil"), Rarity::Ultra).healBonus, 0.5,
               1e-9);
    CHECK_NEAR(fixture().registry.petalStats(petalId("basic"), Rarity::Ultra).healBonus, 0.0,
               1e-12);

    Rig rig;
    Health& health = rig.world.get<Health>(rig.player);
    health.current = health.max - 100.0;
    CombatSystem::landHeal(rig.world, rig.player, health, 10.0);
    CHECK_NEAR(health.current, health.max - 100.0 + 10.0 * kPlayerHealMultiplier, 1e-9);

    rig.equip(0, "basil", Rarity::Mythic);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().healBonus, 0.45, 1e-9);
    // On the ring like any other petal: it has a body, unlike a disc -- one
    // that mobs pass through (user's rule; see combat_tests).
    const auto basils = rig.petals(0);
    CHECK_EQ(basils.size(), std::size_t(1));
    if (!basils.empty()) CHECK(rig.world.get<PetalInstance>(basils[0]).ignoresMobs);
    CHECK(!fixture().registry.petal(petalId("basic")).ignoresMobs);

    // Harmonically: best first, the n-th best at 1/n -- the mythic counts
    // half once an ultra is worn, and a third once two are.
    rig.equip(1, "basil", Rarity::Ultra);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().healBonus, 0.5 + 0.45 / 2.0, 1e-9);
    rig.equip(2, "basil", Rarity::Ultra);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().healBonus, 0.5 + 0.5 / 2.0 + 0.45 / 3.0, 1e-9);
    Health& grown = rig.world.get<Health>(rig.player);
    grown.current = grown.max - 100.0;
    CombatSystem::landHeal(rig.world, rig.player, grown, 10.0);
    CHECK_NEAR(grown.current, grown.max - 100.0 + 10.0 * kPlayerHealMultiplier * 1.9, 1e-9);
    // Still never past max.
    CombatSystem::landHeal(rig.world, rig.player, grown, grown.max);
    CHECK_NEAR(grown.current, grown.max, 1e-9);
}

TEST(a_basil_grows_the_heal_an_amulet_keeps_the_overflow_of) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "basil", Rarity::Ultra);
    rig.equip(1, "amulet", Rarity::Epic);
    rig.settleEquips();
    Health& health = rig.world.get<Health>(rig.player);
    ShieldState& shield = rig.world.get<ShieldState>(rig.player);
    shield.amount = 0.0;
    health.current = health.max - 10.0;
    CombatSystem::landHeal(rig.world, rig.player, health, 40.0);
    CHECK_NEAR(health.current, health.max, 1e-9);
    CHECK_NEAR(shield.amount, (40.0 * kPlayerHealMultiplier * 1.5 - 10.0) * 0.2, 1e-9);
}

TEST(a_disc_resists_collisions_by_florrs_curve_and_does_not_stack) {
    if (!contentLoaded()) return;
    // florr's 1 - 0.91^(tier+1): 9% at common, 1 - 0.91^7 at ultra.
    CHECK_NEAR(fixture().registry.petalStats(petalId("disc"), Rarity::Common).collisionResist,
               0.09, 1e-9);
    CHECK_NEAR(fixture().registry.petalStats(petalId("disc"), Rarity::Ultra).collisionResist,
               1.0 - std::pow(0.91, 7), 1e-9);
    CHECK_NEAR(fixture().registry.petalStats(petalId("basic"), Rarity::Ultra).collisionResist,
               0.0, 1e-12);

    Rig rig;
    rig.world.add<PlayerVisuals>(rig.player);
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().collisionDamageTakenScale, 1.0, 1e-12);
    rig.equip(0, "disc", Rarity::Epic);
    rig.equip(1, "disc", Rarity::Common);
    rig.settleEquips();
    // The better one alone: the common is a spare.
    CHECK_NEAR(rig.modifiers().collisionDamageTakenScale, std::pow(0.91, 4), 1e-9);
    // Worn, never out: florr gives it no body on the ring. Other flowers see
    // it as the ring drawn around this one.
    CHECK(rig.petals(0).empty());
    CHECK(rig.world.get<PlayerVisuals>(rig.player).equipFlags & EquipDisc);
}

TEST(a_bandage_carries_florrs_undead_figures_and_does_not_stack) {
    if (!contentLoaded()) return;
    // florr's tooltip: Undead 1.5 s x 1.4^tier, Unrevivable 30 s, Body
    // Damage -75%, the last two flat.
    const PetalStats mythic = fixture().registry.petalStats(petalId("bandage"), Rarity::Mythic);
    CHECK_NEAR(mythic.undeadMillis, 1500.0 * std::pow(1.4, 5), 1e-6);
    CHECK_NEAR(fixture().registry.petalStats(petalId("bandage"), Rarity::Ultra).undeadMillis,
               1500.0 * std::pow(1.4, 6), 1e-6);
    CHECK_NEAR(fixture().registry.petalStats(petalId("bandage"), Rarity::Common).undeadMillis,
               1500.0, 1e-9);
    CHECK_NEAR(mythic.unrevivableMillis, 0.0, 1e-9);
    CHECK_NEAR(mythic.undeadBodyDamageCut, 0.75, 1e-12);
    CHECK_NEAR(fixture().registry.petalStats(petalId("basic"), Rarity::Ultra).undeadMillis, 0.0,
               1e-12);

    Rig rig;
    rig.settleEquips();
    CHECK_NEAR(rig.modifiers().undeadMillis, 0.0, 1e-12);
    rig.equip(0, "bandage", Rarity::Common);
    rig.equip(1, "bandage", Rarity::Mythic);
    rig.settleEquips();
    // The better one alone: the common is a spare.
    CHECK_NEAR(rig.modifiers().undeadMillis, 1500.0 * std::pow(1.4, 5), 1e-6);
    CHECK_NEAR(rig.modifiers().unrevivableMillis, 0.0, 1e-9);
    CHECK_NEAR(rig.modifiers().undeadBodyDamageScale, 0.25, 1e-12);
    // Worn, never out: florr gives it no body on the ring.
    CHECK(rig.petals(0).empty());
    CHECK(rig.petals(1).empty());
}

TEST(without_an_amulet_overheal_is_simply_lost) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.settleEquips();
    Health& health = rig.world.get<Health>(rig.player);
    ShieldState& shield = rig.world.get<ShieldState>(rig.player);
    shield.amount = 0.0;
    CombatSystem::landHeal(rig.world, rig.player, health, 500.0);
    CHECK_NEAR(health.current, health.max, 1e-9);
    CHECK_NEAR(shield.amount, 0.0, 1e-12);
}

TEST(a_carrot_is_fired_as_a_shot_that_bounces) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "carrot");
    rig.settleEquips();
    rig.setFlags(net::InputAttack);
    int shots = 0;
    bool bounces = true;
    CHECK(rig.tickUntil([&] {
        Query<ProjectileTag, Projectile> query{rig.world};
        query.each([&](Entity, ProjectileTag&, Projectile& projectile) {
            ++shots;
            bounces = bounces && projectile.bouncesOffWalls;
        });
        return shots > 0;
    }, 200));
    CHECK(bounces);
}

// ---------------------------------------------------------------------------
// The Champion's Crown
// ---------------------------------------------------------------------------

namespace {

/// A second flower beside the rig's own, carrying everything the rig's does.
Entity addFlower(Rig& rig, Vec2 at, Realm realm = Realm::Overworld) {
    const Entity e = rig.world.create();
    rig.world.add<PlayerTag>(e);
    rig.world.add<Transform>(e, Transform{at, 0.0, realm});
    rig.world.add<Motion>(e);
    rig.world.add<Knockback>(e);
    rig.world.add<Body>(e, Body{kPlayerBaseRadius, 1.0});
    rig.world.add<Health>(e, Health{maxHealthForLevel(1), maxHealthForLevel(1), 0.0, 0.0});
    rig.world.add<Faction>(e, Faction{Team::Players, false});
    rig.world.add<PlayerProgress>(e, PlayerProgress{});
    rig.world.add<PlayerInput>(e);
    rig.world.add<PlayerModifiers>(e);
    rig.world.add<PlayerLocation>(e);
    rig.world.add<Loadout>(e);
    rig.world.add<PetalRing>(e);
    rig.world.add<Afflictions>(e);
    rig.world.add<HitCooldowns>(e);
    return e;
}

void equipOn(Rig& rig, Entity flower, int slot, const char* id, Rarity rarity = Rarity::Common) {
    LoadoutSlot& s = rig.world.get<Loadout>(flower).slots[static_cast<std::size_t>(slot)];
    s.configIndex = petalId(id);
    s.rarity = rarity;
}

/// The petals a crown has lent `flower`: zero or one.
std::vector<Entity> lentTo(Rig& rig, Entity flower) {
    std::vector<Entity> out;
    Query<PetalTag, PetalInstance> query{rig.world};
    query.each([&](Entity e, PetalTag&, PetalInstance& instance) {
        if (instance.owner == flower && instance.slot == kLentPetalSlot) out.push_back(e);
    });
    return out;
}

/// What a crown of `tier` lends -- a Basic of its own tier. Unique is the tier
/// florr has the crown at, and the one every test below wears.
PetalStats lentStats(Rarity tier = Rarity::Unique) {
    return fixture().registry.petalStats(petalId(kCrownLentPetalId), tier);
}

} // namespace

TEST(a_crown_lends_a_flower_in_reach_a_basic_of_its_tier_once_its_reload_has_run) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.world.add<PlayerVisuals>(rig.player);
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    rig.tick();
    const double lentAt = rig.now;
    // Worn on the head, and on no ring.
    CHECK(rig.world.get<PlayerVisuals>(rig.player).equipFlags & EquipCrown);
    CHECK(rig.petals().empty());
    // A new loan serves the lent petal's reload first, as an equip does.
    CHECK(lentTo(rig, other).empty());
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.now >= lentAt + lentStats().reloadMillis - 1e-9);
    CHECK(rig.now < lentAt + lentStats().reloadMillis + net::kTickMillis);

    const std::vector<Entity> lent = lentTo(rig, other);
    CHECK_EQ(lent.size(), std::size_t(1));
    if (lent.empty()) return;
    const PetalInstance& instance = rig.world.get<PetalInstance>(lent[0]);
    CHECK_EQ(instance.configIndex, petalId(kCrownLentPetalId));
    CHECK(instance.rarity == Rarity::Unique);
    CHECK(instance.creditTo == rig.player);
    CHECK_EQ(rig.world.get<Replicated>(lent[0]).typeIndex, petalId(kCrownLentPetalId));
    CHECK(rig.world.get<Replicated>(lent[0]).rarity == Rarity::Unique);
    CHECK_NEAR(rig.healthOf(lent[0]), lentStats().health, 1e-9);
    // And to its own wearer, on the same reload.
    const std::vector<Entity> own = lentTo(rig, rig.player);
    CHECK_EQ(own.size(), std::size_t(1));
    if (own.empty()) return;
    CHECK(rig.world.get<PetalInstance>(own[0]).creditTo == rig.player);
    CHECK(rig.world.get<PetalInstance>(own[0]).rarity == Rarity::Unique);
}

TEST(a_wearer_keeps_its_own_crowns_basic_beside_a_better_crown) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Common);
    const Entity rival = addFlower(rig, {1100.0, 1000.0});
    equipOn(rig, rival, 0, "champion_crown", Rarity::Mythic);
    CHECK(rig.tickUntil([&] {
        return !lentTo(rig, rig.player).empty() && !lentTo(rig, rival).empty();
    }));
    const PetalInstance& own = rig.world.get<PetalInstance>(lentTo(rig, rig.player).front());
    CHECK(own.creditTo == rig.player);
    CHECK(own.rarity == Rarity::Common);
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, rival).front()).creditTo == rival);
}

TEST(a_crown_lends_at_most_its_count_and_keeps_the_flowers_it_already_lends) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    // Five more flowers than the crown has Basics for, the wearer aside, each
    // a step further out than the last.
    std::vector<Entity> flowers;
    for (int i = 0; i < kCrownMaxLoans + 4; ++i) {
        flowers.push_back(addFlower(rig, {1000.0 + 10.0 * (i + 1), 1000.0}));
    }
    const auto lentCount = [&] {
        int n = static_cast<int>(lentTo(rig, rig.player).size());
        for (const Entity flower : flowers) n += static_cast<int>(lentTo(rig, flower).size());
        return n;
    };
    CHECK(rig.tickUntil([&] { return lentCount() > 0; }));
    rig.tick();
    // The wearer's own, and the nearest flowers to it for the rest.
    CHECK_EQ(lentCount(), kCrownMaxLoans);
    CHECK_EQ(lentTo(rig, rig.player).size(), std::size_t(1));
    for (int i = 0; i < static_cast<int>(flowers.size()); ++i) {
        CHECK_EQ(lentTo(rig, flowers[static_cast<std::size_t>(i)]).size(),
                 std::size_t(i < kCrownMaxLoans - 1 ? 1 : 0));
    }

    // A newcomer nearer than any of them takes nobody's place...
    const Entity last = flowers.back();
    rig.world.get<Transform>(last).position = {1005.0, 1000.0};
    rig.tick(5);
    CHECK(lentTo(rig, last).empty());
    CHECK_EQ(lentCount(), kCrownMaxLoans);

    // ...but a place one leaves is the nearest waiting flower's.
    rig.world.get<Transform>(flowers.front()).position = {1000.0 + kCrownLendRadius + 1.0, 1000.0};
    rig.tick();
    CHECK(lentTo(rig, flowers.front()).empty());
    CHECK(rig.tickUntil([&] { return !lentTo(rig, last).empty(); }));
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, last).front()).creditTo == rig.player);
    CHECK_EQ(lentCount(), kCrownMaxLoans);
}

TEST(a_flower_a_full_crown_turns_away_is_lent_by_the_next_crown_in_reach) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity rival = addFlower(rig, {1500.0, 1000.0});
    equipOn(rig, rival, 0, "champion_crown", Rarity::Unique);
    std::vector<Entity> flowers;
    for (int i = 0; i < kCrownMaxLoans; ++i) {
        flowers.push_back(addFlower(rig, {1000.0 + 5.0 * (i + 1), 1000.0}));
    }
    CHECK(rig.tickUntil([&] { return !lentTo(rig, flowers.front()).empty(); }));
    rig.tick();
    // The wearer and 24 of them are the first crown's; the farthest, turned
    // away, is the rival's though the first is nearer.
    for (int i = 0; i < kCrownMaxLoans; ++i) {
        const std::vector<Entity> lent = lentTo(rig, flowers[static_cast<std::size_t>(i)]);
        CHECK_EQ(lent.size(), std::size_t(1));
        if (lent.empty()) continue;
        CHECK(rig.world.get<PetalInstance>(lent.front()).creditTo ==
              (i < kCrownMaxLoans - 1 ? rig.player : rival));
    }
}

TEST(a_crowns_slot_counts_its_basics_out_on_the_field) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    rig.tick();
    // A gauge: it reads 0 while the loans reload, rather than nothing.
    CHECK_NEAR(rig.slotCounter(0), 0.0, 1e-9);
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    rig.tick();
    CHECK_NEAR(rig.slotCounter(0), 2.0, 1e-9);
    // A mimic of it prints the same figure.
    rig.equip(1, "mimic", Rarity::Unique);
    rig.tick();
    CHECK_NEAR(rig.slotCounter(1), 2.0, 1e-9);

    // One broken is one fewer out.
    rig.world.add<Dead>(lentTo(rig, other).front());
    rig.reap();
    rig.tick(2);
    CHECK_NEAR(rig.slotCounter(0), 1.0, 1e-9);
    // Any other petal has none.
    rig.equip(2, "basic");
    rig.tick();
    CHECK_NEAR(rig.slotCounter(2), -1.0, 1e-9);
}

TEST(a_lent_petal_goes_when_its_flower_leaves_reach_and_a_new_loan_reloads_again) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1000.0 + kCrownLendRadius, 1000.0});
    // The edge of the reach counts.
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    const Entity first = lentTo(rig, other).front();

    rig.world.get<Transform>(other).position = {1000.0 + kCrownLendRadius + 1.0, 1000.0};
    rig.tick();
    CHECK(lentTo(rig, other).empty());
    CHECK(!rig.world.isAlive(first));

    rig.world.get<Transform>(other).position = {1300.0, 1000.0};
    rig.tick();
    const double lentAt = rig.now;
    CHECK(lentTo(rig, other).empty());
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.now >= lentAt + lentStats().reloadMillis - 1e-9);
}

TEST(a_broken_lent_petal_comes_back_after_its_reload) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));

    // Killed in combat: marked Dead, and reaped at the end of the tick.
    rig.world.add<Dead>(lentTo(rig, other).front());
    rig.reap();
    rig.tick();
    const double brokeAt = rig.now;
    CHECK(lentTo(rig, other).empty());
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.now >= brokeAt + lentStats().reloadMillis - 1e-9);
    CHECK(rig.now < brokeAt + lentStats().reloadMillis + net::kTickMillis);

    // Worn down to nothing without combat's mark, it goes all the same.
    rig.damage(lentTo(rig, other).front(), lentStats().health);
    rig.tick();
    CHECK(lentTo(rig, other).empty());
}

TEST(a_loan_ends_with_the_crown) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    rig.unequip(0);
    rig.tick();
    CHECK(lentTo(rig, other).empty());

    // And with its wearer going down.
    rig.equip(0, "champion_crown", Rarity::Unique);
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    rig.world.get<Health>(rig.player).current = 0.0;
    rig.tick();
    CHECK(lentTo(rig, other).empty());
}

TEST(a_crown_lends_nothing_into_another_realm) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1000.0, 1000.0}, Realm::Arena);
    rig.tick(static_cast<int>(lentStats().reloadMillis / net::kTickMillis) + 10);
    CHECK(lentTo(rig, other).empty());
}

TEST(the_nearest_crown_holds_the_loan) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity rival = addFlower(rig, {1500.0, 1000.0});
    equipOn(rig, rival, 0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1400.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, other).front()).creditTo == rival);

    // Walked over to the first, the loan changes hands without a reload.
    const Entity petal = lentTo(rig, other).front();
    rig.world.get<Transform>(other).position = {1100.0, 1000.0};
    rig.tick();
    CHECK(rig.world.isAlive(petal));
    CHECK(rig.world.get<PetalInstance>(petal).creditTo == rig.player);
}

TEST(a_lent_petal_takes_the_last_place_on_its_flowers_ring) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    equipOn(rig, other, 0, "basic");
    // Frozen, as Rig::freezeRing freezes the rig's own: three places, then --
    // the basic, the anchor, and the loan.
    equipOn(rig, other, Rig::kAnchorSlot, "anchor");
    const auto petalIn = [&](int slot) {
        Entity found = NULL_ENTITY;
        Query<PetalTag, PetalInstance> query{rig.world};
        query.each([&](Entity e, PetalTag&, PetalInstance& instance) {
            if (instance.owner == other && instance.slot == slot) found = e;
        });
        return found;
    };
    CHECK(rig.tickUntil([&] {
        return petalIn(0) != NULL_ENTITY && petalIn(Rig::kAnchorSlot) != NULL_ENTITY &&
               !lentTo(rig, other).empty();
    }));
    rig.settleRing();
    const Vec2 centre = rig.position(other);
    const double basicAngle = (rig.position(petalIn(0)) - centre).angle();
    const double lentAngle = (rig.position(lentTo(rig, other).front()) - centre).angle();
    CHECK_NEAR(wrapAngle(lentAngle - basicAngle), -kTau / 3.0, 0.02);
}

TEST(the_lent_basic_follows_the_crowns_tier) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Common);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, other).front()).rarity == Rarity::Common);
    CHECK_NEAR(rig.healthOf(lentTo(rig, other).front()), lentStats(Rarity::Common).health, 1e-9);

    // A better crown is a different petal: the common one goes, and the epic
    // one serves its own reload before it comes out.
    const Entity common = lentTo(rig, other).front();
    rig.equip(0, "champion_crown", Rarity::Epic);
    rig.tick();
    const double swappedAt = rig.now;
    CHECK(!rig.world.isAlive(common));
    CHECK(lentTo(rig, other).empty());
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.now >= swappedAt + lentStats(Rarity::Epic).reloadMillis - 1e-9);
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, other).front()).rarity == Rarity::Epic);
    CHECK_NEAR(rig.healthOf(lentTo(rig, other).front()), lentStats(Rarity::Epic).health, 1e-9);
}

TEST(a_wearers_best_crown_sets_the_tier_and_a_mimic_counts) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Rare);
    rig.equip(3, "champion_crown", Rarity::Common);
    const Entity other = addFlower(rig, {1300.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    CHECK(rig.world.get<PetalInstance>(lentTo(rig, other).front()).rarity == Rarity::Rare);

    // A legendary mimic beside the common crown is a legendary crown.
    rig.equip(4, "mimic", Rarity::Legendary);
    CHECK(rig.tickUntil([&] {
        return !lentTo(rig, other).empty() &&
               rig.world.get<PetalInstance>(lentTo(rig, other).front()).rarity ==
                   Rarity::Legendary;
    }));
}

TEST(a_nearer_crown_of_another_tier_lends_its_own) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "champion_crown", Rarity::Unique);
    const Entity rival = addFlower(rig, {1500.0, 1000.0});
    equipOn(rig, rival, 0, "champion_crown", Rarity::Mythic);
    const Entity other = addFlower(rig, {1400.0, 1000.0});
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    const Entity mythic = lentTo(rig, other).front();
    CHECK(rig.world.get<PetalInstance>(mythic).creditTo == rival);
    CHECK(rig.world.get<PetalInstance>(mythic).rarity == Rarity::Mythic);

    rig.world.get<Transform>(other).position = {1100.0, 1000.0};
    rig.tick();
    CHECK(!rig.world.isAlive(mythic));
    CHECK(rig.tickUntil([&] { return !lentTo(rig, other).empty(); }));
    const PetalInstance& unique = rig.world.get<PetalInstance>(lentTo(rig, other).front());
    CHECK(unique.creditTo == rig.player);
    CHECK(unique.rarity == Rarity::Unique);
}

TEST(lightning_discharges_on_first_garbage_contact_then_reloads_once) {
    Rig rig(testsupport::shippedContent());
    rig.freezeRing();
    rig.equip(0, "lightning");
    rig.settleEquips();
    rig.settleRing();
    const auto petal = rig.petals(0).front();
    const auto at = rig.position(petal);
    const auto garbage = addMob(rig, at);
    rig.world.add<MobType>(garbage, MobType{rig.registry.mobIndex("garbage")});
    const auto next = addMob(rig, at + Vec2{150.0, 0.0});
    CHECK(rig.healthOf(petal) > 0.0);
    rig.tick();
    CHECK_NEAR(rig.healthOf(petal), 0.0, 1e-9);
    const auto* event = firstStrike(rig);
    CHECK(event != nullptr);
    if (event && !event->points.empty()) CHECK_NEAR(distance(event->points.front(), at), 0.0, 1e-9);

    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK(rig.healthOf(garbage) < 10000.0);
    CHECK(rig.healthOf(next) < 10000.0);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_EQ(countStrikes(rig), 1u);
}

TEST(lightning_broken_without_a_mob_contact_does_not_discharge) {
    Rig rig;
    rig.equip(0, "lightning");
    rig.settleEquips();
    const auto petal = rig.petals(0).front();
    addMob(rig, rig.position(petal) + Vec2{180.0, 0.0});
    rig.damage(petal, 1e9);
    rig.tick();
    CHECK(rig.slot(0).broken);
    CHECK_EQ(countStrikes(rig), 0u);
}

TEST(wiki_mechanism_battery_requires_real_damage_and_bounds_chain_targets) {
    CHECK(contentLoaded()); Rig rig; rig.equip(0,"battery"); rig.settleEquips();
    const auto at=rig.world.get<Transform>(rig.player).position;
    const auto source=addMob(rig,at+Vec2{30,0});
    std::vector<Entity> enemies{source};
    for(int n=1;n<=5;++n)enemies.push_back(addMob(rig,at+Vec2{30.0+200.0*n,0}));
    rig.tick(); CHECK_EQ(countStrikes(rig),0u); // touching alone is not damage
    CombatSystem combat;
    const auto hit=combat.applyDamage(rig.world,rig.player,source,1,rig.now);
    CHECK(hit.applied>0);rig.tick();
    CHECK_EQ(countStrikes(rig),1u); CHECK_EQ(batteryCharges(rig),2);
    int targets=0;Query<LightningTarget> q{rig.world};q.each([&](Entity,LightningTarget&){++targets;});
    CHECK_EQ(targets,3); // initial target plus two Common bounces
    const auto* strike=firstStrike(rig); CHECK(strike!=nullptr);
    if(strike){CHECK((strike->flag & net::LightningChained)!=0);CHECK_EQ(strike->points.size(),3u);}
    ricochetCombatTick(rig,combat);
    for(int n=0;n<3;++n)CHECK(rig.world.get<Health>(enemies[n]).current<10000);
    CHECK_NEAR(rig.world.get<Health>(enemies[3]).current,10000,1e-8);
    rig.tick(); CHECK_EQ(countStrikes(rig),1u);
}
TEST(wiki_mechanism_lightning_bypasses_armor_and_dodge) {
    CHECK(contentLoaded());Rig rig;const auto victim=addMob(rig,{1100,1000});
    rig.world.get<Health>(victim).current=rig.world.get<Health>(victim).max=1000;
    rig.world.add<Armor>(victim,Armor{1e9});rig.world.add<Evasion>(victim,Evasion{1.0});
    const auto before=rig.world.get<Health>(victim).current;
    CombatSystem combat;const auto hit=combat.applyDamage(rig.world,victim,rig.player,12,rig.now,DamageKind::Lightning);
    CHECK(!hit.dodged);CHECK_NEAR(hit.applied,12,1e-8);
    CHECK_NEAR(rig.world.get<Health>(victim).current,before-12,1e-8);
}
TEST(wiki_mechanism_blueberry_projectiles_use_lightning_instead_of_zero_physical_damage){
    CHECK(contentLoaded());Rig rig;rig.equip(0,"magic_peas");rig.freezeRing();rig.settleEquips();rig.settleRing();
    rig.setFlags(net::InputAttack);rig.tick();int seen=0;
    Query<ProjectileTag,Projectile> shots{rig.world};shots.each([&](Entity,ProjectileTag&,Projectile& shot){++seen;CHECK_NEAR(shot.damage,120,1e-8);});
    CHECK_EQ(seen,1);
}
TEST(wiki_mechanism_mecha_missile_fires_straight_with_or_without_antennae){
    CHECK(contentLoaded());
    for(const bool antennae:{false,true}){
        Rig rig;rig.equip(0,"mecha_missile");if(antennae)rig.equip(1,"antennae");
        rig.freezeRing();rig.settleEquips();rig.settleRing();
        rig.setFlags(net::InputAttack);rig.tick();int seen=0;
        Query<ProjectileTag,Projectile,Motion> shots{rig.world};
        shots.each([&](Entity,ProjectileTag&,Projectile& shot,Motion& motion){++seen;
            CHECK_NEAR(motion.velocity.length(),600,1e-8);CHECK_NEAR(shot.remainingDistance,2400,1e-8);CHECK_NEAR(shot.damage,35,1e-8);
        });CHECK_EQ(seen,1);
    }
}
TEST(wiki_mechanism_egg_hatches_with_bandage_and_while_undead_with_double_summon_health){
    CHECK(contentLoaded());
    for(const bool undead:{false,true}){Rig rig;rig.equip(0,"bandage",Rarity::Mythic);rig.settleEquips();
        if(undead){const auto attacker=addMob(rig,rig.position(rig.player)+Vec2{60,0});CombatSystem combat;
            combat.applyDamage(rig.world,rig.player,attacker,1e9,rig.now);CHECK(rig.world.has<Undead>(rig.player));CHECK(!rig.world.has<Dead>(rig.player));}
        rig.equip(1,"egg");int count=0;
        CHECK(rig.tickUntil([&]{
            for(const auto shell:rig.petals(1))if(!rig.world.has<Dead>(shell)){
                const auto& instance=rig.world.get<PetalInstance>(shell);
                if(rig.now-instance.spawnedAtMillis<1000){const auto enemy=addMob(rig,rig.position(shell)+Vec2{10,0});CombatSystem combat;
                    CHECK(combat.applyDamage(rig.world,shell,enemy,1e9,rig.now).refused);}
            }
            count=0;Query<Pet,Health,ContactDamage> pets{rig.world};pets.each([&](Entity,Pet& tag,Health& hp,ContactDamage& hit){
            if(tag.owner!=rig.player||tag.slot!=1)return;++count;CHECK_NEAR(hp.max,250,1e-8);CHECK_NEAR(hit.amount,2.0,1e-8);
        });return count==4;},150));
    }
}

TEST(wiki_mechanism_battery_chains_between_large_touching_bodies) {
    CHECK(contentLoaded()); Rig rig; rig.equip(0,"battery");rig.settleEquips();
    const auto at=rig.position(rig.player);
    const auto first=addMob(rig,at+Vec2{200,0});
    const auto second=addMob(rig,at+Vec2{650,0});
    rig.world.get<Body>(first).radius=200;rig.world.get<Body>(second).radius=200;
    CombatSystem combat;combat.applyDamage(rig.world,rig.player,first,1,rig.now);rig.tick();
    ricochetCombatTick(rig,combat);
    CHECK(rig.world.get<Health>(first).current<10000);
    CHECK(rig.world.get<Health>(second).current<10000);
}
TEST(wiki_mechanism_lightning_resolves_once_even_after_a_slow_tick_and_target_movement){
    CHECK(contentLoaded());Rig rig;rig.equip(0,"battery");rig.settleEquips();
    const auto target=addMob(rig,rig.position(rig.player)+Vec2{100,0});CombatSystem combat;
    combat.applyDamage(rig.world,rig.player,target,1,rig.now);rig.tick();
    rig.world.get<Transform>(target).position+=Vec2{600,0};
    SpatialGrid grid;grid.insert(target,Realm::Overworld,rig.position(target),10);
    const auto hp=rig.world.get<Health>(target).current;
    combat.run(rig.world,grid,fixture().registry,rig.now,.25,rig.commands,rig.events);rig.commands.flush();
    CHECK(rig.world.get<Health>(target).current<hp);
    const auto after=rig.world.get<Health>(target).current;
    combat.run(rig.world,grid,fixture().registry,rig.now+250,.25,rig.commands,rig.events);rig.commands.flush();
    CHECK_NEAR(rig.world.get<Health>(target).current,after,1e-8);
}
TEST(wiki_mechanism_shovel_charges_spends_reload_and_expires_without_permanent_invulnerability){
    CHECK(contentLoaded());Rig rig;rig.equip(0,"shovel");rig.settleEquips();
    CHECK(!rig.world.has<BurrowState>(rig.player));rig.tick(65);rig.setFlags(net::InputDefend);rig.tick();
    const auto* state=rig.world.tryGet<BurrowState>(rig.player);CHECK(state&&state->active);if(!state)return;
    CHECK(rig.slot(0).broken);const auto target=addMob(rig,rig.position(rig.player)+Vec2{60,0});CombatSystem combat;
    const auto hp=rig.world.get<Health>(rig.player).current;
    CHECK(combat.applyDamage(rig.world,rig.player,target,10,rig.now).refused);
    CHECK_NEAR(rig.world.get<Health>(rig.player).current,hp,1e-8);
    // The explicit underground predator exception remains hittable.
    rig.world.get<BurrowState>(rig.player).wormIndex=fixture().registry.mobIndex("critter");
    CHECK(combat.applyDamage(rig.world,rig.player,target,1,rig.now).applied>0);
    rig.setFlags(0);rig.tick(65);CHECK(!rig.world.get<BurrowState>(rig.player).active);
    CHECK(combat.applyDamage(rig.world,rig.player,target,1,rig.now).applied>0);
}

TEST(shovel_prevents_a_new_volley_and_forces_surface_time_with_two_slots) {
    CHECK(contentLoaded());
    Rig rig;
    // Grapes would fire on defend before the shovels if actions ran in slot order.
    rig.equip(0, "grapes");
    rig.equip(1, "shovel");
    rig.equip(2, "shovel");
    rig.equip(3, "raindrop");
    rig.settleEquips();
    rig.tick(65);
    auto& cooldown = rig.world.get<PetalSlotState>(rig.player);
    cooldown.raindropReadyAtMillis = rig.now;
    rig.setFlags(net::InputDefend);
    rig.tick();
    const auto* state = rig.world.tryGet<BurrowState>(rig.player);
    CHECK(state && state->active);
    if (!state) return;
    const double surfaceAt = state->untilMillis;
    const double readyAt = state->readyAtMillis;
    Query<ProjectileTag> shots{rig.world};
    std::vector<Entity> entities;
    while (rig.now + net::kTickMillis < surfaceAt) {
        shots.collect(entities);
        CHECK(entities.empty());
        CHECK(rig.world.get<PetalSlotState>(rig.player).raindropReadyAtMillis < rig.now);
        rig.tick();
    }
    rig.tick();
    CHECK(!rig.world.get<BurrowState>(rig.player).active);
    shots.collect(entities);
    CHECK(!entities.empty());
    CHECK(rig.world.get<PetalSlotState>(rig.player).raindropReadyAtMillis > rig.now);
    while (rig.now + net::kTickMillis < readyAt) {
        rig.tick();
        CHECK(!rig.world.get<BurrowState>(rig.player).active);
    }
    rig.tick();
    CHECK(rig.world.get<BurrowState>(rig.player).active);
}

TEST(wiki_mechanism_lentil_attracts_close_sawblade_and_its_mimic) {
    CHECK(contentLoaded());
    for (int slot : {0,1}) {
        Rig rig;rig.equip(0,"sawblade");rig.equip(1,"mimic");rig.equip(2,"lentil");rig.freezeRing();
        rig.settleEquips();rig.settleRing();
        const auto petal=rig.petals(slot).front();const auto orbit=rig.position(petal);
        const auto away=(orbit-rig.position(rig.player)).normalized();
        const auto mob=addAttractor(rig,orbit+away*60,20);
        rig.tick(90);
        CHECK(rig.world.get<PetalInstance>(petal).attractedTo==mob);
        CHECK(distance(rig.position(petal),orbit)>20);
    }
}

TEST(wiki_mechanism_egg_hatches_on_owner_side_of_thin_wall_clear_of_shells){
    CHECK(contentLoaded());Rig rig;rig.useHatchTerrain=true;
    const int column=static_cast<int>(1000/kTileSize)+1;
    for(int row=0;row<rig.terrain.tileRows();++row)rig.terrain.setTile(column,row,Tile::Solid);
    const Vec2 owner{column*kTileSize-40,1000};rig.world.get<Transform>(rig.player).position=owner;
    rig.equip(0,"egg");int count=0;
    CHECK(rig.tickUntil([&]{count=0;Query<Pet,Transform,Body> pets{rig.world};pets.each([&](Entity,Pet& pet,Transform& at,Body& body){
        if(pet.owner!=rig.player)return;++count;
        CHECK(!rig.terrain.segmentBlocked(owner,at.position,Realm::Overworld));
        CHECK(!rig.terrain.resolveWall(at.position,body.radius,Realm::Overworld).collided);
        for(const auto shell:rig.petals(0))if(rig.world.has<Body>(shell))
            CHECK(distance(at.position,rig.position(shell))>=body.radius+rig.world.get<Body>(shell).radius);
    });return count==4;},180));
}


TEST(magic_eye_is_worn_without_spawning_an_orbit_petal) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.world.add<PlayerVisuals>(rig.player);
    rig.equip(0, "magic_eye", Rarity::Unique);
    rig.equip(1, "third_eye", Rarity::Unique);
    rig.tick(200);
    CHECK(rig.petals(0).empty());
    CHECK(rig.petals(1).empty());
    CHECK_EQ(rig.world.get<PlayerVisuals>(rig.player).equipFlags &
             (EquipMagicEye | EquipThirdEye), EquipMagicEye | EquipThirdEye);

    rig.unequip(0);
    rig.tick();
    CHECK_EQ(rig.world.get<PlayerVisuals>(rig.player).equipFlags &
             (EquipMagicEye | EquipThirdEye), EquipThirdEye);
}

TEST(pharaoh_crown_summons_in_a_room_narrower_than_a_beetle) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.useHatchTerrain = true;
    const int col = 4, row = 4;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx || dy) rig.terrain.setTile(col + dx, row + dy, Tile::Solid);
        }
    }
    const Vec2 owner{(col + 0.5) * kTileSize, (row + 0.5) * kTileSize};
    rig.world.get<Transform>(rig.player).position = owner;
    rig.equip(0, "pharaoh_s_crown", Rarity::Unique);

    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }, 140));
    Query<Pet, Transform, Body, MobType>{rig.world}.each(
        [&](Entity, Pet& pet, Transform& at, Body& body, MobType& type) {
            CHECK_EQ(pet.owner, rig.player);
            CHECK_EQ(type.configIndex, registry.mobIndex("mummy_beetle"));
            CHECK(type.rarity == Rarity::Super);
            CHECK(body.radius > kTileSize * 0.5);
            CHECK_NEAR(distance(at.position, owner), 0.0, 1e-7);
            CHECK(!rig.terrain.resolveWall(at.position, kMobWallRadius, at.realm).collided);
            CHECK_NEAR(pet.expiresAtMillis, rig.now + 6000.0, 1e-7);
        });
}

TEST(pharaoh_crown_summons_in_a_crowd_without_stacking_or_extending_duration) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    const auto blocker = addMob(rig, rig.position(rig.player), "ladybug");
    rig.world.get<Body>(blocker).radius = 5000.0;
    rig.equip(0, "pharaoh_s_crown", Rarity::Unique);
    rig.equip(1, "pharaoh_s_crown", Rarity::Apex);

    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }, 140));
    if (rig.petCount() != 1) return;
    const auto first = Query<Pet>{rig.world}.collect().front();
    const auto& type = rig.world.get<MobType>(first);
    CHECK_EQ(rig.world.get<Pet>(first).slot, 1u);
    CHECK(type.rarity == Rarity::Unique);
    const auto& mummy = registry.mob(type.configIndex);
    const double superScale = petalStatScale(Rarity::Super);
    CHECK_NEAR(rig.world.get<Health>(first).max,
               15.0 * mummy.health * mummy.petHealthScale * superScale, 1e-6);
    CHECK_NEAR(rig.world.get<ContactDamage>(first).amount,
               3.0 * mummy.damage * mummy.petDamageScale * superScale, 1e-6);
    rig.tick(19);
    CHECK_EQ(rig.petCount(), 1u);
    rig.tick();
    CHECK_EQ(rig.petCount(), 2u);
    rig.tick(100);
    CHECK_EQ(rig.petCount(), 6u);
    CHECK(!rig.world.isAlive(first));
}

TEST(pharaoh_crown_permanent_hyper_uses_the_hyper_summon_tier) {
    Rig rig(testsupport::shippedContent());
    rig.equip(0, "pharaoh_s_crown", Rarity::PermanentHyper);
    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }, 140));
    for (Entity pet : Query<Pet>{rig.world}.collect()) {
        CHECK(rig.world.get<MobType>(pet).rarity == Rarity::Unique);
    }
}

TEST(pharaoh_crown_waits_for_a_walkable_center_before_using_its_fallback) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.useHatchTerrain = true;
    const Vec2 owner{4.5 * kTileSize, 4.5 * kTileSize};
    rig.world.get<Transform>(rig.player).position = owner;
    rig.terrain.setTile(4, 4, Tile::Solid);
    rig.equip(0, "pharaoh_s_crown", Rarity::Unique);
    rig.settleEquips();
    CHECK_EQ(rig.petCount(), 0u);
    rig.terrain.setTile(4, 4, Tile::Open);
    CHECK(rig.tickUntil([&] { return rig.petCount() == 1; }, 25));
}

TEST(wiki_mechanism_dice_rolls_all_six_faces_and_keeps_equipment_counter_synchronized) {
    CHECK(contentLoaded());
    Rig rig; rig.equip(0,"dice"); rig.settleEquips();
    bool seen[7]{};
    rig.setFlags(net::InputAttack);
    for(int roll=0;roll<256;++roll){
        rig.tick(50);
        const auto& state=rig.world.get<PetalSlotState>(rig.player).slots[0];
        CHECK(state.diceFace>=1 && state.diceFace<=6);
        CHECK_NEAR(rig.slotCounter(0),static_cast<double>(state.diceFace),1e-9);
        seen[state.diceFace]=true;
    }
    for(int face=1;face<=6;++face)CHECK(seen[face]);
    rig.setFlags(0);rig.tick();
    const double restingFace=rig.slotCounter(0);
    bool changed=false;
    for(int roll=0;roll<24;++roll){rig.tick(50);changed|=rig.slotCounter(0)!=restingFace;}
    CHECK(changed);
}

TEST(ant_and_beetle_egg_summons_gain_health_and_damage_at_every_rarity){
    CHECK(contentLoaded());
    const auto& config=fixture().registry.mob(fixture().registry.mobIndex("critter"));
    for(const auto* id:{"egg","beetle_egg"})for(int tier=0;tier<kRarityCount;++tier){
        const auto rarity=clampRarity(tier);Rig rig;rig.equip(0,id,rarity);
        CHECK(rig.tickUntil([&]{return rig.petCount()>0;},2500));
        const auto& source = fixture().registry.petal(fixture().registry.petalIndex(id));
        const auto petRarity = rarity;
        const double scale = petalStrengthScale(rarity) * hyperSummonScale(source, rarity);
        int count=0;Query<Pet,Health,ContactDamage> pets{rig.world};
        pets.each([&](Entity entity,Pet& tag,Health& hp,ContactDamage& hit){
            if(tag.owner!=rig.player)return;++count;
            CHECK_NEAR(hp.max,config.health*scale*25.0,std::max(1e-7,hp.max*1e-10));
            CHECK_NEAR(hit.amount,config.damage*scale*2.0,std::max(1e-7,hit.amount*1e-10));
            CHECK_NEAR(rig.world.get<MobAi>(entity).aggroRange,std::max(kEnemyChaseRange,fixture().registry.mobStats(fixture().registry.mobIndex("critter"),petRarity).aggroRange) + petalTier(petRarity) * kPetAggroRangePerRarity,1e-9);
        });CHECK(count>0);
    }
}

TEST(ant_and_beetle_eggs_duplicate_actual_summons_in_the_leftmost_slot){
    CHECK(contentLoaded());
    for(const auto* id:{"egg","beetle_egg"})for(const auto rarity:{Rarity::Common,Rarity::Unique,Rarity::Apex})for(const int slot:{0,1}){
        Rig rig;rig.world.add<PlayerSkillTree>(rig.player);auto& skills=rig.world.get<PlayerSkillTree>(rig.player).skills;
        skills.set(SkillId::Loadout,4);skills.set(SkillId::Duplicator,3);rig.equip(slot,id,rarity);
        const auto& c=fixture().registry.petal(petalId(id));
        const int expected=petalCopies(c.petCount,&skills,slot,rarityIndex(rarity),c.stackable)*(ladderRarity(rarity)==Rarity::Apex?3:1);
        CHECK(rig.tickUntil([&]{return rig.petCount()==std::size_t(expected);},2500));
        rig.tick(10);CHECK_EQ(rig.petCount(),std::size_t(expected));
    }
}

TEST(every_egg_id_takes_the_same_summon_buff){
    // This build writes only the two hand-made eggs into petals.json; every
    // other `<mob>_egg` is a name the rule has to recognise all the same.
    PetalConfig generated;
    generated.id = "matriarch_egg";
    CHECK_NEAR(eggSummonCombatScale(generated),2.0,1e-9);
    CHECK_NEAR(eggSummonHealthScale(generated),2.5,1e-9);
    PetalConfig ordinary;
    ordinary.id = "basic";
    CHECK_NEAR(eggSummonCombatScale(ordinary),1.0,1e-9);
    CHECK_NEAR(eggSummonHealthScale(ordinary),1.0,1e-9);
}


TEST(hyper_technique_rose_charges_earlier_and_eternal_mimic_cannot_copy_it) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.equip(0, "rose", Rarity::Unique);
    rig.equip(1, "mimic", Rarity::Apex);
    rig.equip(2, "rose", Rarity::Apex);
    rig.settleEquips();
    const auto eternal = rig.petals(0).front();
    const auto copied = rig.petals(1).front();
    const auto hyper = rig.petals(2).front();
    for (const auto rose : {eternal, copied, hyper}) {
        rig.world.get<PetalInstance>(rose).spawnedAtMillis = rig.now - 1100.0;
    }
    auto& health = rig.world.get<Health>(rig.player);
    health.current = health.max - 1.0;
    rig.tick();
    CHECK(!rig.world.get<PetalInstance>(eternal).homing);
    CHECK(!rig.world.get<PetalInstance>(copied).homing);
    CHECK(rig.world.get<PetalInstance>(hyper).homing);
}

TEST(hyper_technique_ground_effects_keep_attack_and_defense_roles) {
    if (!contentLoaded()) return;
    for (const auto flag : {net::InputAttack, net::InputDefend}) {
        Rig rig;
        rig.equip(0, "web", Rarity::Apex);
        rig.equip(1, "electric_web", Rarity::Apex);
        rig.equip(2, "pollen", Rarity::Apex);
        rig.settleEquips();
        rig.setFlags(flag);
        rig.tick();
        const bool defending = flag == net::InputDefend;
        const auto& registry = fixture().registry;
        const auto web = registry.petalStats(petalId("web"), Rarity::Apex);
        const auto electric = registry.petalStats(petalId("electric_web"), Rarity::Apex);
        Query<GroundEffect, Lifetime> fields{rig.world};
        const auto pollen = registry.petalStats(petalId("pollen"), Rarity::Apex);
        CHECK_EQ(fields.count(), static_cast<std::size_t>(pollen.count + 2));
        fields.each([&](Entity e, GroundEffect& field, Lifetime& life) {
            if (field.kind == GroundEffectKind::Poison) {
                CHECK_NEAR(field.radius, 6.0 * (defending ? 1.20 : 1.0), 1e-9);
                CHECK_NEAR(life.remainingSeconds, 6.0, 1e-9);
                return;
            }
            CHECK_NEAR(field.slowFactor, 0.5, 1e-9);
            if (field.kind == GroundEffectKind::ElectricWeb) {
                CHECK_NEAR(field.damagePerHit, electric.damage, 1e-9);
                CHECK_NEAR(field.damageIntervalMillis, 1000.0, 1e-9);
            }
            if (defending) {
                const auto radius = field.kind == GroundEffectKind::Web ? web.webRadius * 1.15
                                                                       : electric.webRadius;
                CHECK_NEAR(field.radius, radius, 1e-9);
            } else {
                CHECK_NEAR(field.radius, 0.0, 1e-9);
                if (const auto* flight = rig.world.tryGet<WebFlight>(e)) {
                    const double duration = field.kind == GroundEffectKind::Web ? 0.35 : 0.28;
                    CHECK_NEAR(flight->duration, duration, 1e-9);
                } else {
                    CHECK(false);
                }
            }
        });
    }
}

TEST(hyper_technique_projectile_flight_preserves_count_and_damage_budget) {
    if (!contentLoaded()) return;
    struct ShotCase {
        const char* id;
        std::uint8_t flag;
        double speed;
        double range;
        bool useInertia = true;
    };
    const ShotCase cases[] = {
        {"missile", net::InputAttack, 1.20, 1.0},
        {"magic_missile", net::InputAttack, 1.0, 1.15},
        {"carrot", net::InputAttack, 1.0, 1.20},
        {"grapes", net::InputAttack, 1.0, 1.15},
        {"grapes", net::InputDefend, 1.0, 1.0},
        {"peas", net::InputDefend, 1.0, 1.0, false},
        {"magic_peas", net::InputAttack, 1.0, 1.0},
        {"magic_peas", net::InputDefend, 1.15, 1.0},
    };
    for (const auto& scenario : cases) {
        for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
            Rig rig;
            rig.equip(0, scenario.id, tier);
            rig.settleEquips();
            const Vec2 drift{45.0, -70.0};
            rig.world.get<Motion>(rig.player).velocity = drift;
            rig.setFlags(scenario.flag);
            CHECK(rig.tickUntil([&] { return Query<ProjectileTag>{rig.world}.count() > 0; }, 20));

            const auto& registry = fixture().registry;
            const auto id = petalId(scenario.id);
            const auto& config = registry.petal(id);
            const auto stats = registry.petalStats(id, tier);
            const bool hyper = tier == Rarity::Apex;
            const double speed = config.projectile.speed * (hyper ? scenario.speed : 1.0);
            const double range = config.projectile.distance * (hyper ? scenario.range : 1.0);
            const Vec2 carried = !hyper || scenario.useInertia ? drift : Vec2{};
            Query<ProjectileTag, Projectile, Motion, Lifetime> shots{rig.world};
            CHECK_EQ(shots.count(), static_cast<std::size_t>(config.projectile.count));
            shots.each([&](Entity, ProjectileTag&, Projectile& shot, Motion& motion, Lifetime& life) {
                CHECK_NEAR((motion.velocity - carried).length(), speed, 1e-8);
                CHECK_NEAR(shot.remainingDistance, range, 1e-8);
                CHECK_NEAR(life.remainingSeconds, range / speed, 1e-8);
                CHECK_NEAR(shot.damage, config.lightningDamage ? stats.lightningDamagePerHit : stats.damage, 1e-8);
                CHECK_EQ(shot.bouncesOffWalls, config.id == "carrot");
            });
        }
    }
}

TEST(hyper_technique_rescue_has_extra_reach_but_cannot_cross_realms) {
    if (!contentLoaded()) return;
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig;
        rig.freezeRing();
        rig.equip(0, "yggdrasil", tier);
        rig.settleEquips();
        rig.settleRing();
        const auto leaf = rig.petals(0).front();
        const auto at = rig.position(leaf) + Vec2{90.0, 0.0};
        const auto addBody = [&](Realm realm) {
            const auto e = rig.world.create();
            rig.world.add<PlayerTag>(e);
            rig.world.add<Loadout>(e);
            rig.world.add<PetalRing>(e);
            rig.world.add<Transform>(e, Transform{at, 0.0, realm});
            rig.world.add<Health>(e, Health{0.0, 100.0});
            rig.world.add<Dead>(e);
            return e;
        };
        const auto otherRealm = addBody(Realm::Arena);
        const auto here = addBody(Realm::Overworld);
        rig.tick();
        CHECK(rig.world.has<Dead>(otherRealm));
        CHECK_EQ(rig.world.has<Dead>(here), tier == Rarity::Unique);
        CHECK_EQ(rig.world.get<Health>(leaf).alive(), tier == Rarity::Unique);
    }
}

TEST(hyper_technique_compass_finds_nearby_high_tiers_in_defense) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.equip(0, "compass", Rarity::Apex);
    const auto low = wildMob(rig, {1050, 1000}, Rarity::Rare);
    const auto close = wildMob(rig, {1300, 1000}, Rarity::Super);
    const auto far = wildMob(rig, {4000, 1000}, Rarity::Apex);
    rig.settleEquips();
    const auto compass = rig.petals(0).front();
    rig.tick(12);
    CHECK_EQ(rig.world.get<PetalInstance>(compass).compassTarget, far);
    rig.setFlags(net::InputDefend);
    rig.tick(12);
    CHECK_EQ(rig.world.get<PetalInstance>(compass).compassTarget, close);
    CHECK(rig.world.get<PetalInstance>(compass).compassTarget != low);
}

TEST(bone_keeps_armor_and_hyper_guard_blocks_more_wild_recoil) {
    const auto& registry = testsupport::shippedContent();
    for (const auto tier : {Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        Rig rig(registry);
        rig.equip(0, "bone", tier);
        rig.settleEquips();
        const auto bone = rig.petals(0).front();
        const auto mob = addMob(rig, {1500, 1500}, "ladybug");
        CombatSystem combat;
        const double initial = rig.world.get<Armor>(bone).amount;
        rig.setFlags(net::InputDefend);
        const auto recoil = combat.applyDamage(rig.world, bone, mob, initial * 1.2, rig.now, DamageKind::Recoil);
        CHECK_NEAR(recoil.applied, initial * (tier == Rarity::Unique ? 0.2 : 0.1), 1e-6);
        CHECK_NEAR(rig.world.get<Armor>(bone).amount, initial, 1e-8);
        const auto direct = combat.applyDamage(rig.world, bone, mob, initial * 1.2, rig.now + 1000.0);
        CHECK_NEAR(direct.applied, initial * 0.2, 1e-6);
        CHECK_NEAR(rig.world.get<Armor>(bone).amount, initial, 1e-8);
    }
}

TEST(hyper_technique_mana_orb_and_shell_charge_earlier_and_fill_small_gaps) {
    const auto& registry = testsupport::shippedContent();
    for (const auto tier : {Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        for (const bool mana : {false, true}) {
            Rig rig(registry);
            rig.equip(0, mana ? "mana_orb" : "shell", tier);
            const double cap = rig.world.get<Health>(rig.player).max;
            if (mana) rig.world.add<ManaPool>(rig.player, ManaPool{1e9, 1e9});
            else rig.world.add<ShieldState>(rig.player, ShieldState{cap});
            rig.settleEquips();
            const auto petal = rig.petals(0).front();
            const double expectedCharge = mana
                ? (tier == Rarity::Unique ? 1500.0 : 1000.0)
                : (tier == Rarity::Unique ? 1000.0 : 750.0);
            const auto& instance = rig.world.get<PetalInstance>(petal);
            CHECK(!instance.homing);
            const double spawned = instance.spawnedAtMillis;
            if (mana) {
                auto& pool = rig.world.get<ManaPool>(rig.player);
                pool.current = pool.max - 1.0;
            } else {
                rig.world.get<ShieldState>(rig.player).amount = cap - 1.0;
            }
            CHECK(rig.tickUntil([&] {
                return rig.world.get<PetalInstance>(petal).homing;
            }, 40));
            CHECK_NEAR(rig.now - spawned, expectedCharge, 1e-7);
            CHECK(rig.tickUntil([&] {
                if (!mana) return rig.world.get<ShieldState>(rig.player).amount == cap;
                const auto& pool = rig.world.get<ManaPool>(rig.player);
                return pool.current == pool.max;
            }, 80));
        }
    }
}

TEST(hyper_technique_magic_leaf_and_orb_fill_the_wearers_mana_without_draining_it) {
    const auto& registry = testsupport::shippedContent();
    for (const auto tier : {Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        Rig rig(registry);
        rig.equip(0, "magic_leaf", tier);
        rig.equip(1, "mana_orb", tier);
        rig.settleEquips();
        const auto ally = rig.world.create();
        rig.world.add<PlayerTag>(ally);
        rig.world.add<Transform>(ally, Transform{rig.position(rig.player) + Vec2{20, 0}});
        rig.world.add<Health>(ally, Health{100.0, 100.0});
        rig.world.add<Faction>(ally, Faction{Team::Players, false});
        rig.world.add<PetalRing>(ally);
        auto& cactus = rig.world.add<Loadout>(ally).slots[0];
        cactus.configIndex = registry.petalIndex("magic_cactus");
        rig.world.add<ManaPool>(ally, ManaPool{});
        rig.tick();
        CHECK(rig.world.get<ManaPool>(ally).max > 0.0);
        const double maximum = rig.world.get<ManaPool>(rig.player).max;
        rig.world.get<ManaPool>(rig.player).current = maximum * 0.25;
        rig.setFlags(net::InputDefend);
        rig.tick();
        const double regen = registry.petalStats(registry.petalIndex("magic_leaf"), tier).passiveManaPerSecond;
        const double scale = tier == Rarity::Unique ? 1.0 : 1.25;
        CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current,
                   maximum * 0.25 + regen * scale * net::kTickSeconds, 1e-7);
        rig.world.get<ManaPool>(rig.player).current = maximum * 0.95;
        CHECK(rig.tickUntil([&] { return rig.world.get<ManaPool>(rig.player).current == maximum; }, 100));
        rig.tick(40);
        CHECK_NEAR(rig.world.get<ManaPool>(rig.player).current, maximum, 1e-7);
        CHECK_NEAR(rig.world.get<ManaPool>(ally).current, 0.0, 1e-7);
    }
}

TEST(high_tier_blueberries_deal_less_damage_at_the_original_volley_rate) {
    const auto& registry = testsupport::shippedContent();
    for (const auto rarity : {Rarity::Super, Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        const double damagePerBerry = rarity == Rarity::Super ? 262440.0 :
            rarity == Rarity::Unique ? 413343.0 : 826686.0;
        Rig rig(registry);
        rig.equip(0, "magic_peas", rarity);
        rig.equip(1, "magic_leaf", rarity);
        rig.equip(2, "magic_leaf", rarity);
        rig.settleEquips();
        rig.settleRing();
        rig.setFlags(net::InputAttack | net::InputDefend);

        const auto nextVolley = [&] {
            return rig.tickUntil([&] { return Query<ProjectileTag, Projectile>{rig.world}.count() > 0; }, 30);
        };
        const auto readVolley = [&] {
            Query<ProjectileTag, Projectile> shots{rig.world};
            const auto volley = shots.collect();
            CHECK_EQ(volley.size(), std::size_t(4));
            for (const auto e : volley) {
                CHECK_NEAR(rig.world.get<Projectile>(e).damage, damagePerBerry, 1e-6);
                rig.world.destroy(e);
            }
        };

        CHECK(nextVolley());
        const double firstAt = rig.now;
        readVolley();
        CHECK(nextVolley());
        CHECK_NEAR(rig.now - firstAt, 350.0, 1e-9);
        readVolley();
    }
}

TEST(berry_reload_is_visible_before_a_held_attack_fires) {
    const auto& content = testsupport::shippedContent();
    for (const auto id : {"grapes", "peas", "magic_peas"}) {
        Rig rig(content);
        rig.equip(0, id);
        rig.equip(1, "mana_orb", Rarity::Unique);
        rig.setFlags(net::InputAttack | net::InputDefend);
        CHECK(rig.tickUntil([&] { return !rig.petals(0).empty(); }));
        const auto ammo = rig.petals(0);
        CHECK(!ammo.empty());
        if (ammo.empty()) continue;

        const double loadedAt = rig.world.get<PetalInstance>(ammo.front()).spawnedAtMillis;
        Query<Projectile> shots{rig.world};
        CHECK_EQ(shots.count(), 0u);
        CHECK(rig.tickUntil([&] { return shots.count() > 0; }, 500));
        CHECK(rig.now >= loadedAt + kBerryLoadMillis);
        CHECK_EQ(shots.count(), ammo.size());

        for (const auto& event : rig.events.events()) {
            CHECK(event.kind != net::EventKind::Explosion);
        }
    }
}

TEST(meteor_aimed_into_a_wall_lands_on_the_near_side) {
    Rig rig(testsupport::shippedContent());
    rig.useHatchTerrain = true;
    const int column = 10;
    const double edge = column * kTileSize;
    const Vec2 origin{edge - 32.0, 4000.0};
    rig.world.get<Transform>(rig.player).position = origin;
    for (int y = 0; y < rig.terrain.tileRows(); ++y) rig.terrain.setTile(column, y, Tile::Solid);
    rig.equip(0, "meteor", Rarity::Apex);
    rig.settleEquips();
    const auto ammo = rig.petals(0);
    CHECK_EQ(ammo.size(), 1u);
    if (ammo.empty()) return;
    rig.world.get<Transform>(ammo.front()).position = origin + Vec2{90.0, 0.0};
    rig.world.get<PetalInstance>(ammo.front()).ringVelocity = {};
    rig.world.get<PetalRing>(rig.player).spin = 0.0;

    rig.setFlags(net::InputAttack | net::InputDefend);
    rig.tick();
    const auto rocks = Query<PlantedMeteor>{rig.world}.collect();
    CHECK_EQ(rocks.size(), 1u);
    if (rocks.empty()) return;
    const auto at = rig.position(rocks.front());
    CHECK(at.x > origin.x);
    CHECK(at.x < edge);
    CHECK(!rig.terrain.segmentBlocked(origin, at, Realm::Overworld));
    CHECK_NEAR(rig.world.get<Body>(rocks.front()).radius, 180.0, 1e-6);
}

TEST(honey_moves_on_the_throw_tick_and_travels_390_units_while_running) {
    Rig rig;
    rig.freezeRing();
    rig.equip(0, "honey");
    rig.settleEquips();
    rig.world.get<PetalRing>(rig.player).spin = 0.0;
    rig.settleRing();

    const auto honey = rig.petals(0).front();
    constexpr double runningSpeed = 450.0;
    rig.world.get<Motion>(rig.player).velocity = {runningSpeed, 0.0};
    rig.world.get<Transform>(rig.player).position.x += runningSpeed * net::kTickSeconds;
    rig.setFlags(net::InputAttack);
    rig.tick();

    const auto baits = Query<HoneyBait>{rig.world}.collect();
    CHECK_EQ(baits.size(), 1u);
    if (baits.size() != 1u) return;
    const auto bait = baits.front();
    const Vec2 from = rig.position(honey);
    CHECK(rig.position(bait).x - from.x > runningSpeed * net::kTickSeconds);
    CHECK_NEAR(rig.world.get<HoneyBait>(bait).travel + distance(rig.position(bait), from),
               390.0, 1e-6);
    rig.setFlags(0);
    for (int i = 0; i < 40; ++i) {
        rig.world.get<Transform>(rig.player).position.x += runningSpeed * net::kTickSeconds;
        rig.tick();
    }
    CHECK_NEAR(distance(rig.position(bait), from), 390.0, 1e-6);
}

TEST(honey_launches_when_attack_and_defend_are_both_held) {
    if (!contentLoaded()) return;
    Rig rig(testsupport::shippedContent());
    rig.equip(0, "honey");
    rig.settleEquips();

    Query<HoneyBait, Transform> baits{rig.world};
    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK_EQ(baits.count(), 0u);

    rig.setFlags(net::InputAttack | net::InputDefend);
    rig.tick();
    CHECK_EQ(baits.count(), 1u);
    if (baits.count() != 1) return;
    Entity bait = NULL_ENTITY;
    baits.each([&](Entity e, HoneyBait&, Transform&) { bait = e; });
    const Vec2 launched = rig.position(bait);
    rig.setFlags(0);
    rig.tick(5);
    CHECK(distance(launched, rig.position(bait)) > 0.0);
}

TEST(honey_ring_in_a_wall_releases_the_bait_on_the_flowers_side) {
    if (!contentLoaded()) return;
    Rig rig;
    rig.useHatchTerrain = true;
    const int column = 10;
    const double wallEdge = column * kTileSize;
    const Vec2 origin{wallEdge - 32.0, 4000.0};
    rig.world.get<Transform>(rig.player).position = origin;
    for (int y = 0; y < rig.terrain.tileRows(); ++y) rig.terrain.setTile(column, y, Tile::Solid);

    rig.equip(0, "honey");
    rig.freezeRing();
    rig.settleEquips();
    rig.world.get<PetalRing>(rig.player).spin = 0.0;
    rig.settleRing();
    const auto honey = rig.petals(0);
    CHECK_EQ(honey.size(), 1u);
    if (honey.empty()) return;
    CHECK(rig.terrain.blocked(rig.position(honey.front()), Realm::Overworld));

    rig.setFlags(net::InputAttack);
    rig.tick();
    Entity bait = NULL_ENTITY;
    Query<HoneyBait>{rig.world}.each([&](Entity e, HoneyBait&) { bait = e; });
    CHECK(bait != NULL_ENTITY);
    if (bait == NULL_ENTITY) return;
    const Vec2 from = rig.position(bait);
    CHECK(!rig.terrain.blocked(from, Realm::Overworld));

    rig.setFlags(0);
    rig.tick(8);
    const Vec2 landed = rig.position(bait);
    CHECK(landed.x > from.x);
    CHECK(landed.x < wallEdge);
    CHECK(!rig.terrain.segmentBlocked(from, landed, Realm::Overworld));
}

TEST(hyper_technique_dahlia_returns_faster_and_honey_flies_further) {
    if (!contentLoaded()) return;
    double eternalReturn = 0.0;
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig;
        rig.freezeRing();
        rig.equip(0, "dahlia", tier);
        rig.equip(1, "honey", tier);
        rig.settleEquips();
        rig.settleRing();
        const auto dahlia = rig.petals(0).front();
        const Vec2 at = rig.position(rig.player) + Vec2{170.0, 0.0};
        rig.world.get<Transform>(dahlia).position = at;
        auto& instance = rig.world.get<PetalInstance>(dahlia);
        instance.homing = true;
        instance.homingSinceMillis = rig.now;
        instance.ringVelocity = {};
        rig.world.get<Health>(rig.player).current *= 0.5;
        rig.setFlags(net::InputAttack);
        rig.tick();
        const double advanced = distance(at, rig.position(dahlia));
        if (tier == Rarity::Unique) eternalReturn = advanced;
        else {
            CHECK(eternalReturn > 0.0);
            CHECK_NEAR(advanced, eternalReturn * 1.25, 1e-6);
        }
        Query<HoneyBait, Lifetime> baits{rig.world};
        CHECK_EQ(baits.count(), 1u);
        baits.each([&](Entity, HoneyBait& bait, Lifetime& life) {
            const double speed = bait.velocity.length();
            CHECK_NEAR(bait.travel + speed * net::kTickSeconds,
                       tier == Rarity::Apex ? 487.5 : 390.0, 1e-9);
            CHECK_NEAR(bait.travel / speed + net::kTickSeconds, 2.0, 1e-9);
            CHECK_NEAR(life.remainingSeconds, 8.0, 1e-9);
        });
    }
}

TEST(thrown_honey_outlasts_a_wild_mob_hit_and_expires_on_its_own) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.equip(0, "honey", Rarity::Unique);
    rig.settleEquips();
    rig.setFlags(net::InputAttack);
    rig.tick();
    rig.setFlags(0);
    const auto baits = Query<HoneyBait>{rig.world}.collect();
    CHECK_EQ(baits.size(), 1u);
    if (baits.empty()) return;
    const auto bait = baits.front();
    const double meteor = registry.petalStats(registry.petalIndex("meteor"), Rarity::Unique).health;
    const double durability = rig.world.get<Health>(bait).max;
    CHECK(durability > meteor);
    const auto mob = addMob(rig, rig.position(bait) + Vec2{10.0, 0.0});
    CombatSystem combat;
    CHECK(combat.applyDamage(rig.world, bait, mob, durability * 2.0, rig.now).refused);
    CHECK_NEAR(rig.world.get<Health>(bait).current, durability, 1e-8);
    rig.tick(150);
    CHECK(rig.world.isAlive(bait));
    rig.tick(11);
    CHECK(!rig.world.isAlive(bait));
}

TEST(hyper_technique_wing_gains_only_attack_lunge) {
    if (!contentLoaded()) return;
    std::array<double, 2> idle{};
    std::array<double, 2> attacking{};
    for (int i = 0; i < 2; ++i) {
        Rig rig;
        rig.freezeRing();
        rig.equip(0, "wing", i == 0 ? Rarity::Unique : Rarity::Apex);
        rig.settleEquips();
        rig.settleRing();
        const auto wing = rig.petals(0).front();
        idle[i] = distance(rig.position(wing), rig.position(rig.player));
        rig.setFlags(net::InputAttack);
        for (int frame = 0; frame < 100; ++frame) {
            rig.tick();
            attacking[i] = std::max(attacking[i], distance(rig.position(wing), rig.position(rig.player)));
        }
    }
    CHECK_NEAR(idle[0], idle[1], 1e-6);
    CHECK(attacking[1] > attacking[0] + 10.0);
}


TEST(hyper_technique_pearl_recalls_sooner_without_changing_its_launch) {
    if (!contentLoaded()) return;
    double eternalWindow = 0.0;
    double eternalSpeed = 0.0;
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig;
        rig.freezeRing();
        rig.equip(0, "pearl", tier);
        rig.settleEquips();
        rig.settleRing();
        const Entity pearl = rig.petals(0).front();
        rig.setFlags(net::InputAttack);
        rig.tick();
        const auto& thrown = rig.world.get<PetalInstance>(pearl);
        CHECK(thrown.thrown);
        const double speed = thrown.flightVelocity.length();
        rig.setFlags(net::InputDefend);
        rig.tick();
        const auto& recalled = rig.world.get<PetalInstance>(pearl);
        CHECK(!recalled.thrown);
        const double window = recalled.glideUntilMillis - rig.now;
        CHECK(window > 0.0);
        if (tier == Rarity::Unique) {
            eternalWindow = window;
            eternalSpeed = speed;
        } else {
            CHECK_NEAR(speed, eternalSpeed, 1e-6);
            CHECK_NEAR(window, eternalWindow * 0.80, 1e-6);
        }
    }
}

TEST(hyper_technique_icosahedron_seeks_further_inside_its_original_leash) {
    if (!contentLoaded()) return;
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig;
        rig.equip(0, "icosahedron", tier);
        rig.settleEquips();
        const Entity petal = rig.petals(0).front();
        rig.world.get<Transform>(petal).position = {1000, 1000};
        const auto skipped = addMob(rig, {1220, 1000});
        CHECK_EQ(CombatSystem::ricochetTarget(rig.world, petal, rig.now), NULL_ENTITY);
        const auto target = addMob(rig, {1200, 1000});
        CHECK_EQ(CombatSystem::ricochetTarget(rig.world, petal, rig.now),
                 tier == Rarity::Apex ? target : NULL_ENTITY);
        rig.world.get<Transform>(rig.player).position = {500, 1000};
        CHECK_EQ(CombatSystem::ricochetTarget(rig.world, petal, rig.now), NULL_ENTITY);
        CHECK(rig.world.isAlive(skipped));
    }
}


TEST(hyper_catalog_covers_shipped_petals_and_blocks_other_tiers_and_copies) {
    const auto& registry = testsupport::shippedContent();
    CHECK(registry.petalCount() > 130);
    CHECK_EQ(registry.petalCount(), sizeof(kHyperTechniques) / sizeof(*kHyperTechniques));
    std::size_t gameplay = 0;
    std::size_t reserved = 0;
    for (std::uint16_t i = 0; i < registry.petalCount(); ++i) {
        const auto& id = registry.petal(i).id;
        const auto* info = hyperTechniqueInfo(id, Rarity::Apex);
        CHECK(info != nullptr);
        if (!info) continue;
        CHECK(!hyperTechniqueInfo(id, Rarity::Unique));
        CHECK(!hyperTechniqueInfo(id, Rarity::Apex, true));
        CHECK(!hyperTechniqueInfo(id, Rarity::Admin));
        CHECK(!std::string_view(info->chinese).empty());
        CHECK(!std::string_view(info->english).empty());
        CHECK(!std::string_view(hyperMysteryHint(info->technique, true)).empty());
        CHECK(!std::string_view(hyperMysteryHint(info->technique, false)).empty());
        if (info->technique == HyperTechnique::Material || info->technique == HyperTechnique::Preserved) {
            ++reserved;
        } else {
            ++gameplay;
            CHECK(info->technique != HyperTechnique::None);
        }
    }
    CHECK_EQ(reserved, 4u);
    CHECK_EQ(gameplay, 133u);
    CHECK(hyperForgeEligible("fragment"));
    CHECK_EQ(hyperForgeOutput("fragment"), std::string("mjolnir"));
    CHECK(!hyperForgeEligible("timed_mjolnir"));
    CHECK(!hyperForgeEligible("blood_sacrifice"));
}

TEST(hyper_catalog_nazar_keeps_its_cooldown_across_a_loadout_swap_and_spares_small_hits) {
    const auto& shipped = testsupport::shippedContent();
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    if (!error.empty()) return;
    World world;
    const auto flower = world.create();
    world.add<PlayerTag>(flower);
    world.add<Health>(flower, Health{1000.0, 1000.0});
    world.add<Faction>(flower, Faction{Team::Players, false});
    world.add<Loadout>(flower);
    world.add<PlayerInput>(flower).current.flags = net::InputDefend;
    const auto mob = world.create();
    world.add<MobTag>(mob);
    world.add<Faction>(mob, Faction{Team::Hostiles, false});
    const auto equip = [&] {
        const auto petal = world.create();
        auto& instance = world.add<PetalInstance>(petal);
        instance.rarity = Rarity::Apex;
        instance.configIndex = shipped.petalIndex("nazar_amulet");
        instance.owner = flower;
        world.get<Loadout>(flower).spawned = {petal};
        return petal;
    };
    const auto first = equip();
    CombatSystem combat;
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 200.0, 1000.0).applied, 200.0, 1e-8);
    CHECK(!world.has<HyperPetalTimers>(flower));
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 500.0, 2000.0).applied, 200.0, 1e-8);
    CHECK_NEAR(world.get<HyperPetalTimers>(flower).wardReadyAtMillis, 32000.0, 1e-8);
    world.destroy(first);
    equip();
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 400.0, 3000.0).applied, 400.0, 1e-8);
    CHECK_NEAR(world.get<HyperPetalTimers>(flower).wardReadyAtMillis, 32000.0, 1e-8);
}

TEST(magic_cotton_spends_only_the_mana_needed_and_passes_overflow_to_health) {
    std::string error;
    if (!loadContent(testsupport::dataDir(), error)) { CHECK(false); return; }
    const auto cotton = content().petalIndex("magic_cotton");
    CHECK(!content().petalStats(cotton, Rarity::Common).breakable);
    CHECK(!content().petalStats(cotton, Rarity::Apex).breakable);

    World world;
    const auto flower = world.create();
    world.add<PlayerTag>(flower);
    world.add<Health>(flower, Health{100.0, 100.0});
    world.add<ManaPool>(flower, ManaPool{20.0, 20.0});
    world.add<Faction>(flower, Faction{Team::Players, false});
    world.add<Loadout>(flower);
    const auto mob = world.create();
    world.add<MobTag>(mob);
    world.add<Faction>(mob, Faction{Team::Hostiles, false});
    const auto equip = [&](int slot, Rarity rarity) {
        const auto petal = world.create();
        auto& instance = world.add<PetalInstance>(petal);
        instance.configIndex = cotton;
        instance.owner = flower;
        instance.rarity = rarity;
        instance.slot = slot;
        world.get<Loadout>(flower).spawned.push_back(petal);
        return petal;
    };
    const auto first = equip(0, Rarity::Common);
    const auto reserve = equip(kLoadoutActiveSlots, Rarity::Apex);
    CombatSystem combat;
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 4.0, 1000.0).applied, 0.0, 1e-8);
    CHECK_NEAR(world.get<ManaPool>(flower).current, 12.0, 1e-8);
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 10.0, 2000.0, DamageKind::Poison).applied, 4.0, 1e-8);
    CHECK_EQ(world.get<ManaPool>(flower).current, 0.0);
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 10.0, 3000.0).applied, 10.0, 1e-8);

    const auto second = equip(1, Rarity::Super);
    const double perMana = magicCottonDamagePerMana(Rarity::Super);
    world.get<ManaPool>(flower).current = 10.0;
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 2.0 * perMana, 4000.0).applied, 0.0, 1e-8);
    CHECK_NEAR(world.get<ManaPool>(flower).current, 8.0, 1e-8);
    CHECK(!world.has<Dead>(first));
    CHECK(!world.has<Dead>(second));
    CHECK(!world.has<Dead>(reserve));
    world.add<Dead>(second);
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 5.0, 5000.0).applied, 1.0, 1e-8);
    CHECK_EQ(world.get<ManaPool>(flower).current, 0.0);
}

TEST(hyper_catalog_magic_cotton_saves_mana_on_guarded_wild_body_hits_only) {
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    if (!error.empty()) return;
    World world;
    const auto flower = world.create();
    world.add<PlayerTag>(flower);
    world.add<Health>(flower, Health{1000.0, 1000.0});
    world.add<ManaPool>(flower, ManaPool{50.0, 100.0});
    world.add<Faction>(flower, Faction{Team::Players, false});
    world.add<Loadout>(flower);
    world.add<PlayerInput>(flower).current.flags = net::InputDefend;
    const auto mob = world.create();
    world.add<MobTag>(mob);
    world.add<Faction>(mob, Faction{Team::Hostiles, false});
    for (std::uint8_t i = 0; i < 2; ++i) {
        const auto petal = world.create();
        auto& instance = world.add<PetalInstance>(petal);
        instance.rarity = Rarity::Apex;
        instance.configIndex = content().petalIndex("magic_cotton");
        instance.owner = flower;
        instance.slot = i;
        world.get<Loadout>(flower).spawned.push_back(petal);
    }
    CombatSystem combat;
    const double perMana = magicCottonDamagePerMana(Rarity::Apex);
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 100.0, 1000.0).applied, 0.0, 1e-8);
    CHECK_NEAR(world.get<ManaPool>(flower).current, 50.0 - 100.0 / (1.25 * perMana), 1e-8);
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 100.0, 2000.0, DamageKind::Lightning).applied, 0.0, 1e-8);
    CHECK_NEAR(world.get<ManaPool>(flower).current, 50.0 - 100.0 / (1.25 * perMana) - 100.0 / perMana, 1e-8);
    world.get<PlayerInput>(flower).current.flags = net::InputAttack;
    CHECK_NEAR(combat.applyDamage(world, flower, mob, 100.0, 3000.0).applied, 0.0, 1e-8);
    CHECK_NEAR(world.get<ManaPool>(flower).current, 50.0 - 100.0 / (1.25 * perMana) - 200.0 / perMana, 1e-8);
}


TEST(hyper_catalog_air_compacts_its_added_size_only_while_defending) {
    if (!contentLoaded()) return;
    for (const Rarity tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig;
        rig.equip(0, "air", tier);
        rig.settleEquips();
        const double neutral = rig.world.get<Body>(rig.player).radius;
        const double base = playerRadiusForLevel(1);
        CHECK(neutral > base);
        rig.setFlags(net::InputDefend);
        rig.tick();
        CHECK_NEAR(rig.world.get<Body>(rig.player).radius,
                   base + (neutral - base) * (tier == Rarity::Apex ? 0.88 : 1.0), 1e-6);
    }
}

namespace {

// Isolate one live ring contact without waiting for the orbit to reach a mob.
Entity hyperContactPetal(Rig& rig, const char* id, Rarity tier, bool lowerCopy = false) {
    const Entity petal = rig.world.create();
    rig.world.add<PetalTag>(petal);
    rig.world.add<Transform>(petal, Transform{{1200.0, 1000.0}, 0.0});
    rig.world.add<Body>(petal, Body{10.0, 1.0});
    rig.world.add<Health>(petal, Health{1e9, 1e9});
    rig.world.add<Faction>(petal, Faction{Team::Players, false});
    auto& instance = rig.world.add<PetalInstance>(petal);
    instance.owner = rig.player;
    instance.configIndex = rig.registry.petalIndex(id);
    instance.rarity = tier;
    instance.mimicked = lowerCopy;
    rig.equip(0, lowerCopy ? "mimic" : id, tier);
    return petal;
}

Entity hyperContactMob(Rig& rig, Entity petal, Rarity tier = Rarity::Apex) {
    const Entity mob = addMob(rig, rig.position(petal) + Vec2{2.0, 0.0}, "ladybug");
    rig.world.get<Health>(mob) = Health{1e9, 1e9};
    rig.world.get<MobType>(mob).rarity = tier;
    return mob;
}

} // namespace

TEST(hyper_dice_buffs_noncritical_hits_on_wounded_wild_mobs) {
    const auto& registry = testsupport::shippedContent();
    for (const auto tier : {Rarity::Unique, Rarity::Apex, Rarity::PermanentHyper}) {
        Rig rig(registry);
        rig.setFlags(net::InputAttack);
        const auto die = hyperContactPetal(rig, "dice", tier);
        const double base = registry.petalStats(registry.petalIndex("dice"), tier).damage;
        CombatSystem combat;
        bool nonCritical = false;
        for (int i = 0; i < 10; ++i) {
            const auto mob = hyperContactMob(rig, die);
            rig.world.get<Health>(mob).current = 5e8;
            ricochetCombatTick(rig, combat);
            const double dealt = 5e8 - rig.world.get<Health>(mob).current;
            if (dealt > base * 2.0) {
                CHECK_NEAR(dealt, base * 35.0, 1e-6);
            } else {
                nonCritical = true;
                CHECK_NEAR(dealt, base * (tier == Rarity::Unique ? 1.0 : 1.25), 1e-6);
            }
            rig.world.destroy(mob);
            rig.now += 2000.0;
        }
        CHECK(nonCritical);
    }
}

TEST(durable_sawblade_and_rock_follow_up_contact_and_spend_durability) {
    const auto& registry = testsupport::shippedContent();
    for (const auto* id : {"sawblade", "rock"}) {
        for (const auto tier : {Rarity::Super, Rarity::Apex}) {
            Rig rig(registry);
            rig.setFlags(net::InputAttack);
            const auto petal = hyperContactPetal(rig, id, tier);
            const auto mob = hyperContactMob(rig, petal);
            rig.world.ensure<ContactDamage>(mob).amount = 100.0;
            const auto base = registry.petalStats(registry.petalIndex(id), tier).damage;
            CombatSystem combat;
            ricochetCombatTick(rig, combat);
            const double factor = tier == Rarity::Apex ? 1.5 : 1.0;
            CHECK_NEAR(1e9 - rig.world.get<Health>(mob).current, base * factor, 1e-6);
            CHECK_NEAR(1e9 - rig.world.get<Health>(petal).current, 100.0 * factor, 1e-6);
        }
    }
}

TEST(hyper_followup_rock_and_corn_cover_distinct_physical_hits) {
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    if (!error.empty()) return;
    for (const Rarity tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig(content());
        rig.setFlags(net::InputDefend);
        const Entity rock = hyperContactPetal(rig, "rock", tier);
        const Entity corn = hyperContactPetal(rig, "corn", tier);
        const Entity mob = hyperContactMob(rig, rock);
        const Entity shot = rig.world.create();
        rig.world.add<Projectile>(shot).owner = mob;
        rig.world.add<Faction>(shot, Faction{Team::Hostiles, false});
        CombatSystem combat;
        const auto hit = [&](Entity victim, Entity source, DamageKind kind) {
            return combat.applyDamage(rig.world, victim, source, 100.0, rig.now, kind).applied;
        };
        CHECK_NEAR(hit(rock, mob, DamageKind::Recoil), tier == Rarity::Apex ? 85.0 : 100.0, 1e-7);
        CHECK_NEAR(hit(rock, shot, DamageKind::Direct), 100.0, 1e-7);
        CHECK_NEAR(hit(corn, shot, DamageKind::Direct), tier == Rarity::Apex ? 82.0 : 100.0, 1e-7);
        CHECK_NEAR(hit(corn, shot, DamageKind::Lightning), 100.0, 1e-7);
        CHECK_NEAR(hit(corn, mob, DamageKind::Recoil), 100.0, 1e-7);
        rig.setFlags(net::InputAttack);
        CHECK_NEAR(hit(rock, mob, DamageKind::Recoil), 100.0, 1e-7);
        CHECK_NEAR(hit(corn, shot, DamageKind::Direct), 100.0, 1e-7);
    }
}

TEST(hyper_followup_salt_answers_only_physical_high_tier_wild_hits) {
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    if (!error.empty()) return;
    Rig rig(content());
    rig.equip(0, "salt", Rarity::Apex);
    rig.setFlags(net::InputDefend);
    auto& mods = rig.world.get<PlayerModifiers>(rig.player);
    mods.damageReflection = 0.20;
    mods.damageReflectionRarity = Rarity::Apex;
    const auto mob = addMob(rig, {1200.0, 1000.0}, "ladybug");
    const auto reflect = [&](Rarity tier, DamageKind kind, std::uint8_t flags) {
        rig.world.get<MobType>(mob).rarity = tier;
        rig.world.get<Health>(rig.player) = Health{1000.0, 1000.0};
        rig.setFlags(flags);
        const double before = rig.healthOf(mob);
        CombatSystem combat;
        combat.applyDamage(rig.world, rig.player, mob, 100.0, rig.now, kind);
        return before - rig.healthOf(mob);
    };
    CHECK_NEAR(reflect(Rarity::Apex, DamageKind::Direct, net::InputDefend), 22.0, 1e-7);
    CHECK_NEAR(reflect(Rarity::Apex, DamageKind::Direct, net::InputAttack), 20.0, 1e-7);
    CHECK_NEAR(reflect(Rarity::Apex, DamageKind::Lightning, net::InputDefend), 20.0, 1e-7);
    CHECK_NEAR(reflect(Rarity::Unique, DamageKind::Direct, net::InputDefend),
               reflect(Rarity::Unique, DamageKind::Direct, net::InputAttack), 1e-7);
}

TEST(hyper_followup_fang_heals_from_actual_loss_with_a_bounded_guard_bonus) {
    const auto& registry = testsupport::shippedContent();
    for (const bool guarding : {false, true}) {
        Rig rig(registry);
        const auto fang = hyperContactPetal(rig, "fang", Rarity::Apex);
        const auto mob = hyperContactMob(rig, fang);
        rig.world.get<Health>(rig.player) = Health{30000.0, 100000.0};
        rig.world.get<PlayerModifiers>(rig.player).healBonus = 0.25;
        rig.setFlags(guarding ? net::InputDefend : net::InputAttack);
        const double before = rig.healthOf(mob);
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        const double dealt = before - rig.healthOf(mob);
        CHECK(dealt > 0.0);
        const double base = dealt * registry.petalStats(registry.petalIndex("fang"), Rarity::Apex).lifesteal *
                            kPlayerHealMultiplier * 1.25;
        CHECK_NEAR(rig.healthOf(rig.player), 30000.0 + base +
                   (guarding ? std::min(base * 0.15, 2000.0) : 0.0), 1e-6);
    }
}

TEST(hyper_followup_jelly_and_sawblade_apply_short_wild_effects) {
    const auto& registry = testsupport::shippedContent();
    {
        Rig rig(registry);
        rig.setFlags(net::InputDefend);
        const auto jelly = hyperContactPetal(rig, "jelly", Rarity::Apex);
        const auto mob = hyperContactMob(rig, jelly);
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        const auto* afflictions = rig.world.tryGet<Afflictions>(mob);
        CHECK(afflictions != nullptr);
        if (afflictions) {
            CHECK_NEAR(afflictions->slowFactor, 0.85, 1e-7);
            CHECK_NEAR(afflictions->slowUntilMillis, rig.now + 400.0, 1e-7);
        }
        auto& prior = rig.world.get<Afflictions>(mob);
        prior.slowFactor = 0.5;
        prior.slowUntilMillis = rig.now + 150.0;
        const double expiry = prior.slowUntilMillis;
        rig.now += 50.0;
        ricochetCombatTick(rig, combat);
        CHECK_NEAR(rig.world.get<Afflictions>(mob).slowFactor, 0.5, 1e-7);
        CHECK_NEAR(rig.world.get<Afflictions>(mob).slowUntilMillis, expiry, 1e-7);
    }
    for (const bool copied : {false, true}) {
        Rig rig(registry);
        rig.setFlags(net::InputAttack);
        const auto blade = hyperContactPetal(rig, "sawblade", Rarity::Apex);
        if (copied) rig.equip(0, "mimic", Rarity::Apex);
        const auto mob = hyperContactMob(rig, blade);
        rig.world.add<Armor>(mob, Armor{10000.0});
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        const auto* afflictions = rig.world.tryGet<Afflictions>(mob);
        CHECK_NEAR(afflictions ? afflictions->shred(rig.now) : 0.0, copied ? 0.0 : 1000.0, 1e-7);
        if (copied || !afflictions) continue;
        CHECK_NEAR(afflictions->armorShredUntilMillis, rig.now + 1000.0, 1e-7);
        combat.applyArmorShred(rig.world, mob, 1000.0, rig.now);
        const double burExpiry = rig.world.get<Afflictions>(mob).armorShredUntilMillis;
        rig.now += 100.0;
        ricochetCombatTick(rig, combat);
        CHECK_NEAR(rig.world.get<Afflictions>(mob).armorShredUntilMillis, burExpiry, 1e-7);
    }
}

TEST(hyper_followup_plank_bonus_is_for_wild_projectiles_while_defending) {
    const auto& registry = testsupport::shippedContent();
    const auto damage = [&](bool defending, bool wild) {
        Rig rig(registry);
        rig.setFlags(defending ? net::InputDefend : net::InputAttack);
        const auto plank = hyperContactPetal(rig, "plank", Rarity::Apex);
        const auto shooter = hyperContactMob(rig, plank);
        rig.world.get<Transform>(shooter).position = {1500.0, 1000.0};
        if (!wild) rig.world.add<Pet>(shooter);
        const auto shot = rig.world.create();
        rig.world.add<ProjectileTag>(shot);
        rig.world.add<Projectile>(shot).owner = shooter;
        rig.world.add<Transform>(shot, Transform{rig.position(plank) + Vec2{2.0, 0.0}, 0.0});
        rig.world.add<Body>(shot, Body{10.0, 1.0});
        rig.world.add<Health>(shot, Health{1e9, 1e9});
        rig.world.add<Faction>(shot, Faction{Team::Hostiles, false});
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        return 1e9 - rig.healthOf(shot);
    };
    const double base = damage(false, true);
    CHECK(base > 0.0);
    CHECK_NEAR(damage(true, true), base * 1.10, 1e-6);
    CHECK_NEAR(damage(true, false), base, 1e-6);
}

TEST(hyper_followup_tomato_ripens_early_without_raising_the_ceiling) {
    const auto& registry = testsupport::shippedContent();
    for (const auto [age, stages] : {std::pair{4999.0, 5.0}, {5000.0, 7.0}, {12000.0, 14.0}, {20000.0, 14.0}}) {
        Rig rig(registry);
        rig.now = 50000.0;
        const auto tomato = hyperContactPetal(rig, "tomato", Rarity::Apex);
        rig.world.get<PetalInstance>(tomato).spawnedAtMillis = rig.now - age;
        const auto mob = hyperContactMob(rig, tomato);
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        CHECK_NEAR(1e9 - rig.healthOf(mob), stages *
                   registry.petalStats(registry.petalIndex("tomato"), Rarity::Apex).damage, 1e-6);
    }
}

TEST(hyper_followup_capacitor_extends_only_early_guard_discharges) {
    if (!contentLoaded()) return;
    for (const auto flag : {net::InputDefend, net::InputAttack}) {
        Rig rig;
        rig.setFlags(flag);
        const auto mob = touchCapacitor(rig, Rarity::Apex);
        rig.tick();
        pullAway(rig, mob);
        rig.tick();
        double radius = 0.0;
        Query<GroundEffect, LightningBurst> fields{rig.world};
        fields.each([&](Entity, GroundEffect& field, LightningBurst&) { radius = field.radius; });
        CHECK_NEAR(radius, flag == net::InputDefend ? 1150.0 : 1000.0, 1e-7);
        CHECK_NEAR(lastStrikeDamage(rig), net::kTickMillis * petalStrengthScale(Rarity::Apex), 1e-6);
    }
}

TEST(hyper_followup_stinger_focuses_near_aim_only) {
    const auto& registry = testsupport::shippedContent();
    for (const Rarity tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig(registry);
        rig.equip(0, "stinger", tier);
        rig.setFlags(net::InputAttack);
        rig.settleEquips();
        const auto stingers = rig.petals(0);
        CHECK(stingers.size() > 1);
        if (stingers.size() <= 1) continue;
        const auto spacing = [&](bool aligned) {
            for (int tick = 0; tick < 240; ++tick) {
                auto& frame = rig.world.get<PlayerInput>(rig.player).current;
                frame.aimDistance = 500;
                frame.aimAngle = rig.world.get<Transform>(stingers.front()).angle + (aligned ? 0.0 : kPi);
                rig.tick();
            }
            return distance(rig.position(stingers[0]), rig.position(stingers[1]));
        };
        const double unfocused = spacing(false);
        CHECK(unfocused > 0.0);
        CHECK_NEAR(spacing(true) / unfocused, tier == Rarity::Apex ? 0.8 : 1.0, 0.02);
    }
}

TEST(hyper_final_basic_rally_is_defense_only_even_at_full_health) {
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    if (!error.empty()) return;
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        for (const bool copy : {false, true}) {
            Rig rig(content());
            const auto basic = hyperContactPetal(rig, "basic", tier, copy);
            const auto mob = hyperContactMob(rig, basic);
            CombatSystem combat;
            rig.setFlags(net::InputDefend);
            CHECK_NEAR(combat.applyDamage(rig.world, basic, mob, 100.0, rig.now, DamageKind::Recoil).applied,
                       tier == Rarity::Apex && !copy ? 88.0 : 100.0, 1e-7);
            rig.setFlags(net::InputAttack);
            CHECK_NEAR(combat.applyDamage(rig.world, basic, mob, 100.0, rig.now, DamageKind::Recoil).applied,
                       100.0, 1e-7);
        }
    }
}

TEST(hyper_final_ranged_techniques_have_one_bounded_shot_and_do_not_copy_eternal) {
    const auto& registry = testsupport::shippedContent();
    struct ShotCase { const char* id; double range; double damage; };
    for (const auto& test : {ShotCase{"clay",250,0.55}, {"hornet_missile",425,0.65},
                             {"laser",390,0.45}, {"wasp_missile",350,0.55}, {"javascript",330,0.45}}) {
        for (const int mode : {0, 1, 2}) {
            Rig rig(registry);
            rig.equip(0, test.id, mode == 0 ? Rarity::Apex : Rarity::Unique);
            if (mode == 2) rig.equip(1, "mimic", Rarity::Apex);
            if (std::string_view(test.id) == "javascript") {
                rig.equip(2, "ram", Rarity::Apex);
                rig.equip(3, "computer_chip", Rarity::Apex);
            }
            rig.settleEquips();
            rig.setFlags(net::InputAttack | net::InputDefend);
            rig.tick();
            Query<Projectile> shots{rig.world};
            CHECK_EQ(shots.count(), mode == 0 ? 1u : 0u);
            shots.each([&](Entity, Projectile& shot) {
                CHECK(shot.wildOnly);
                CHECK_NEAR(shot.remainingDistance, test.range, 1e-7);
                const double kit = std::string_view(test.id) == "javascript" ? 1.12 : 1.0;
                const auto base = registry.petalStats(registry.petalIndex(test.id), Rarity::Apex).damage;
                CHECK_NEAR(shot.damage, base * test.damage * kit, 1e-7);
                if (std::string_view(test.id) == "wasp_missile") CHECK(shot.waveAmplitude > 0.0);
            });
        }
    }
}

TEST(hyper_final_clay_shot_spends_one_impact_on_wild_mobs) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.equip(0, "clay", Rarity::Apex);
    rig.settleEquips();
    rig.setFlags(net::InputAttack);
    rig.tick();

    Entity shot = NULL_ENTITY;
    Query<Projectile>{rig.world}.each([&](Entity e, Projectile&) { shot = e; });
    CHECK(shot != NULL_ENTITY);
    if (shot == NULL_ENTITY) return;
    const Vec2 origin{1500.0, 1000.0};
    rig.world.get<Transform>(shot).position = origin;
    rig.world.get<Projectile>(shot).lastPosition = origin;
    const auto summon = addMob(rig, origin, "ladybug");
    rig.world.add<Pet>(summon);
    const auto first = addMob(rig, origin + Vec2{1.0, 0.0}, "ladybug");
    const auto second = addMob(rig, origin + Vec2{2.0, 0.0}, "ladybug");
    rig.world.get<Health>(shot) = Health{1e9, 1e9};

    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    CHECK_EQ(rig.healthOf(summon), 10000.0);
    CHECK_EQ((rig.healthOf(first) < 10000.0) + (rig.healthOf(second) < 10000.0), 1);
    CHECK(rig.world.has<Dead>(shot));
}

TEST(hyper_final_triangle_counts_only_other_hyper_triangles) {
    const auto& registry = testsupport::shippedContent();
    const auto damage = [&](Rarity other) {
        Rig rig(registry);
        rig.setFlags(net::InputAttack);
        const auto triangle = hyperContactPetal(rig, "triangle", Rarity::Apex);
        auto& mods = rig.world.get<PlayerModifiers>(rig.player);
        ++mods.triangleCounts[rarityIndex(Rarity::Apex)];
        ++mods.triangleCounts[rarityIndex(other)];
        const auto mob = hyperContactMob(rig, triangle);
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        return 1e9 - rig.healthOf(mob);
    };
    const double base = damage(Rarity::Unique);
    CHECK(base > 0.0);
    // Same-tier stacking adds the petal's ordinary Triangle bonus before the
    // Hyper technique, so compare against the same-grade base and stack stats.
    const auto stats = registry.petalStats(registry.petalIndex("triangle"), Rarity::Apex);
    CHECK_NEAR(base, stats.damage, 1e-6);
    TriangleCounts counts{};
    counts[rarityIndex(Rarity::Apex)] = 2;
    CHECK_NEAR(damage(Rarity::Apex), (base + triangleBonusDamage(Rarity::Apex, counts)) * 1.06, 1e-6);
}

TEST(permanent_hyper_uses_the_same_stats_and_techniques_as_contested_hyper) {
    const auto& registry = testsupport::shippedContent();
    CHECK(registry.petalCount() > 0);
    for (std::uint16_t i = 0; i < registry.petalCount(); ++i) {
        const auto h = registry.petalStats(i, Rarity::Apex);
        const auto gold = registry.petalStats(i, Rarity::PermanentHyper);
        CHECK_EQ(h.damage, gold.damage);
        CHECK_EQ(h.health, gold.health);
        CHECK_EQ(h.reloadMillis, gold.reloadMillis);
        CHECK_EQ(h.count, gold.count);
        CHECK_EQ(h.cameraZoom, gold.cameraZoom);
        CHECK(hyperTechniqueInfo(registry.petal(i).id, Rarity::Apex) ==
              hyperTechniqueInfo(registry.petal(i).id, Rarity::PermanentHyper));
    }
    const auto damage = [&](Rarity tier) {
        Rig rig(registry);
        rig.setFlags(net::InputAttack);
        const auto triangle = hyperContactPetal(rig, "triangle", tier);
        rig.world.get<PlayerModifiers>(rig.player).triangleCounts[rarityIndex(Rarity::Apex)] = 2;
        const auto mob = hyperContactMob(rig, triangle);
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        return 1e9 - rig.healthOf(mob);
    };
    CHECK(damage(Rarity::Apex) > 0.0);
    CHECK_NEAR(damage(Rarity::PermanentHyper), damage(Rarity::Apex), 1e-6);
}

TEST(hyper_final_chip_extends_only_hyper_ship_tracking) {
    const auto& registry = testsupport::shippedContent();
    for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
        Rig rig(registry);
        rig.equip(0, "spaceship", tier);
        rig.equip(1, "computer_chip", Rarity::Apex);
        rig.equip(2, "ram", Rarity::Apex);
        rig.equip(3, "cube", Rarity::Apex);
        rig.settleEquips();
        rig.setFlags(net::InputAttack);
        rig.tick();
        Query<Projectile> shots{rig.world};
        CHECK_EQ(shots.count(), 1u);
        shots.each([&](Entity, Projectile& shot) {
            CHECK_NEAR(shot.seekRange, tier == Rarity::Apex ? 1100.0 : 1000.0, 1e-7);
        });
    }
}

TEST(hyper_final_break_and_defense_effects_use_existing_field_rules) {
    const auto& registry = testsupport::shippedContent();
    for (const char* id : {"broccoli", "coral", "glitch"}) {
        for (const auto tier : {Rarity::Unique, Rarity::Apex}) {
            Rig rig(registry);
            rig.equip(0, id, tier);
            rig.settleEquips();
            const auto petals = rig.petals(0);
            CHECK_EQ(petals.size(), 1u);
            if (petals.empty()) continue;
            const auto mode = std::string_view(id);
            if (mode == "glitch") {
                rig.freezeRing();
                rig.setFlags(net::InputDefend);
                rig.tick();
                CHECK_EQ(Query<GroundEffect>{rig.world}.count(), 0u);
                CHECK(rig.world.get<Health>(petals.front()).alive());
                addMob(rig, rig.position(petals.front()) + Vec2{10.0, 0.0}, "ladybug");
            } else {
                rig.damage(petals.front(), rig.healthOf(petals.front()) + 1.0);
            }
            rig.tick();
            Query<GroundEffect> fields{rig.world};
            CHECK_EQ(fields.count(), tier == Rarity::Apex ? 1u : 0u);
            fields.each([&](Entity, GroundEffect& field) {
                CHECK_NEAR(field.radius, mode == "broccoli" ? 80.0 : mode == "coral" ? 60.0 : 75.0, 1e-7);
                CHECK(!field.slowsFlowers);
            });
        }
    }
}

TEST(hyper_final_coin_kill_grants_bounded_pickup_range_without_stacking) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    const auto coin = hyperContactPetal(rig, "coin", Rarity::Apex);
    const auto mob = hyperContactMob(rig, coin);
    rig.world.get<Health>(mob).current = 1.0;
    CombatSystem combat;
    ricochetCombatTick(rig, combat);
    const auto* timers = rig.world.tryGet<HyperPetalTimers>(rig.player);
    CHECK(timers != nullptr);
    if (!timers) return;
    CHECK_NEAR(timers->coinPickupUntilMillis, rig.now + 4000.0, 1e-7);
    rig.tick();
    const double boosted = rig.world.get<PlayerModifiers>(rig.player).magnetism;
    rig.tick(90);
    CHECK_NEAR(boosted - rig.world.get<PlayerModifiers>(rig.player).magnetism, 40.0, 1e-7);
}

TEST(hyper_final_leaf_cuts_wild_poison_only_once_per_window) {
    const auto& registry = testsupport::shippedContent();
    Rig rig(registry);
    rig.equip(0, "leaf", Rarity::Apex);
    rig.settleEquips();
    auto& health = rig.world.get<Health>(rig.player);
    health.current = health.max * 0.30;
    const auto mob = addMob(rig, {1600.0, 1000.0}, "ladybug");
    auto& poison = rig.world.get<Afflictions>(rig.player);
    poison.poisonSource = mob;
    poison.poisonPerSecond = 1.0;
    poison.poisonUntilMillis = rig.now + 4000.0;
    const double before = poison.poisonUntilMillis;
    rig.tick(2);
    CHECK_NEAR(rig.world.get<Afflictions>(rig.player).poisonUntilMillis, before - 400.0, 1e-7);
}

TEST(hyper_final_rice_finishes_wild_mobs_only_while_attacking) {
    const auto& registry = testsupport::shippedContent();
    const auto damage = [&](Rarity tier, auto flag, double fraction, bool copy = false) {
        Rig rig(registry);
        rig.setFlags(flag);
        const auto rice = hyperContactPetal(rig, "rice", tier, copy);
        const auto mob = hyperContactMob(rig, rice);
        const double before = 1e9 * fraction;
        rig.world.get<Health>(mob).current = before;
        CombatSystem combat;
        ricochetCombatTick(rig, combat);
        return before - rig.healthOf(mob);
    };
    const double base = damage(Rarity::Apex, net::InputAttack, 0.25);
    CHECK(base > 0.0);
    CHECK_NEAR(damage(Rarity::Apex, net::InputAttack, 0.24), base * 1.08, 1e-6);
    CHECK_NEAR(damage(Rarity::Apex, net::InputDefend, 0.24), base, 1e-6);
    CHECK_NEAR(damage(Rarity::Apex, net::InputAttack, 0.24, true), base, 1e-6);
    CHECK_NEAR(damage(Rarity::Unique, net::InputAttack, 0.24),
               damage(Rarity::Unique, net::InputAttack, 0.25), 1e-6);
}

TEST(hyper_technique_privet_berry_saves_short_poison_and_cleanses_long_poison) {
    Rig rig(testsupport::shippedContent());
    rig.equip(0, "privet_berry", Rarity::Apex);
    rig.settleEquips();
    const auto berry = rig.petals(0).front();
    auto& poison = rig.world.get<Afflictions>(rig.player);
    poison.poisonPerSecond = 10.0;
    poison.poisonUntilMillis = rig.now + 500.0;
    rig.setFlags(net::InputDefend);
    rig.tick();
    CHECK(rig.world.get<Health>(berry).alive());
    const double expires = rig.now + 2000.0;
    rig.world.get<Afflictions>(rig.player).poisonUntilMillis = expires;
    rig.tick();
    CHECK(!rig.world.get<Health>(berry).alive());
    CHECK_NEAR(rig.world.get<Afflictions>(rig.player).poisonUntilMillis, expires - 1250.0, 1e-7);
}
