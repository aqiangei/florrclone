#pragma once
// What a dead mob leaves behind, and who is allowed to take it.
//
// A drop is a real entity: it sits on the ground, it is replicated, it expires,
// and it is picked up by walking over it. That last part is the whole reason
// magnetism is a RADIUS and not a pull force -- dragging the item into the
// player consumes it before any snapshot ever carried it, and the client is
// left with nothing to animate.
//
// Eligibility exists so that killing a mob is worth doing. Only the ranked
// contributors see a normal mob drop, and each of them may collect it once.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "server/replication.h"
#include "server/squads.h"
#include "shared/core/types.h"
#include "shared/core/world.h"
#include "shared/game/components.h"
#include "shared/game/config.h"
#include "shared/game/constants.h"
#include "shared/game/rarity.h"
#include "shared/game/spatial.h"
#include "shared/game/terrain.h"
#include "shared/game/drop_rates.h"

namespace flix {

class ServerWorkers;

// ---------------------------------------------------------------------------
// Components owned by this system
// ---------------------------------------------------------------------------

/// Set on a mob once its drops have been rolled.
///
/// Death is a component that survives to the end of the tick so later systems
/// can read it, which means "is dead" is not by itself a one-shot signal. This
/// tag is: without it a mob whose corpse lingers for a second tick would pay
/// out its whole table again.
struct LootAwarded {};

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------

/// Sub-Ultra loot stays on the server briefly for nearby pickup. Higher
/// tiers use their original lifetimes. Event drops set their own lifetime.
inline constexpr double kShortGroundDropSeconds = 8.0;
inline constexpr std::array<double, kRarityCount> kDropLifetimeByRarity = {
    kShortGroundDropSeconds, kShortGroundDropSeconds, kShortGroundDropSeconds,
    kShortGroundDropSeconds, kShortGroundDropSeconds, kShortGroundDropSeconds,
    80.0, 120.0, 300.0, 600.0, 600.0, 0.0, 600.0, 600.0,
};

/// Collision radius a drop is given. It has no Motion and nothing pushes it;
/// the body exists so the broadphase files it and the pickup query finds it.
inline constexpr double kDropBodyRadius = 14.0;

/// Half-size the per-tick wall push resolves a drop with: the reference's
/// DROPPED_ITEM_RADIUS, a 30x30 hitbox. Deliberately not the body above -- that
/// one only has to be found by the pickup query, this one decides whether an
/// item ends up embedded in a rock.
inline constexpr double kDropWallRadius = 15.0;

/// Normal stacks can be retired early under crowding. Event drops use their
/// own lifecycle and do not spend the normal ground budget.
inline constexpr std::size_t kMaxGroundDropsPerRealm = 512;
inline constexpr std::size_t kMaxGroundDrops = 1536;

// ---------------------------------------------------------------------------
// The drop table
// ---------------------------------------------------------------------------

/// JSON-authored mob drop tables resolved against loaded content: ids become
/// indices once, and the per-tick path is an array lookup. The source data is
/// data/mob_drops.json, staged into the build's data/ directory beside mobs.json.
class DropTables {
public:
    /// What one authored row hands out.
    ///
    /// The `random` sentinel names no petal at all, yet stays in the table: a
    /// row is a drop this mob HAS, and above common every drop it has comes
    /// out. Mobs drop no consumables; load skips a `consumable` row outright.
    enum class Kind : std::uint8_t {
        Petal = 0,     ///< `petalIndex` names the item outright
        RandomPetal,   ///< resolved per copy against droppablePetals()
    };

    struct Entry {
        std::uint16_t petalIndex = kNoPetal;
        Kind kind = Kind::Petal;
        /// Authored rarity index (legacy field name). Read on a common or
        /// unusual mob, and on the one graded row of a rarer one; above
        /// unusual that row is graded against the mob that left it instead.
        int rarityOffset = 0;
        /// On a common mob, the chance the row drops at all. On every mob
        /// above one the row always drops, and above an unusual one this is
        /// its WEIGHT in the draw for which row is the graded drop -- see
        /// LootSystem::pickFeatured.
        double probability = 0.0;
        int minCount = 1;
        int maxCount = 1;
        DropRates rates;
        DropRates magicRates;
        std::uint16_t magicPetal = kNoPetal;
        int rollGroup = 0;
    };

    /// Parses `path`, then resolves its ids against `content`. The JSON's
    /// authored item rarities remain absolute rarity indices.
    bool load(const ContentRegistry& content, const std::string& path, std::string& errorOut);

    /// Resolves previously loaded data against `content`. If callers did not
    /// explicitly load a table (unit tests), this finds the staged default.
    void link(const ContentRegistry& content);

    bool linkedTo(const ContentRegistry& content) const;

    /// The authored rows for a mob, one per line of JSON. Empty for a mob
    /// that drops nothing, and for an index the content does not define.
    /// This is the common-mob table: each row is an independent chance.
    const std::vector<Entry>& forMob(std::uint16_t mobIndex) const;

    /// The same table with one row per drop TYPE, for every mob above common.
    ///
    /// Those mobs hand out every row they have, so the authored habit of
    /// giving one petal two lines -- "rose common 0.5" beside "rose uncommon
    /// 0.1", which used to be two independent chances at one item -- would
    /// otherwise pay out two roses per kill. The lines merge into one drop
    /// whose probability is the chance that either of them would have fired.
    const std::vector<Entry>& guaranteedForMob(std::uint16_t mobIndex) const;

    /// Mob ids in the source table the loaded content does not define.
    const std::vector<std::string>& unresolved() const { return unresolved_; }

    /// The petals a `random` row may hand out, in catalogue order: everything
    /// except admin petals, the two cutters, magic forms (converted into,
    /// never dropped) and the eggs of mobs marked noEggDrop. One list, because
    /// the reference keeps one -- the item spawner used to re-derive the rule
    /// and got it wrong.
    const std::vector<std::uint16_t>& droppablePetals() const { return droppable_; }

    /// One uniformly chosen droppable petal, for the `random` sentinel rows.
    /// Rolled per COPY rather than per row, so an apex garbage leaves ten
    /// different petals rather than ten of one. Only one that comes at
    /// `rarity` (petalComesAt): a pick that does not is drawn again, so every
    /// other petal's odds -- and every draw that never hits one -- are as
    /// they were.
    std::uint16_t randomPetal(Rng& rng, Rarity rarity) const;

private:
    struct SourceEntry {
        std::string mobId;
        std::string petalId;
        Kind kind = Kind::Petal;
        int rarityOffset = 0;  ///< authored rarity index (legacy field name)
        double probability = 0.0;
        int minCount = 1;
        int maxCount = 1;
        DropRates rates;
        DropRates magicRates;
        std::string magicPetalId;
        int rollGroup = 0;
    };

    void resolve(const ContentRegistry& content);
    void loadDefault(const ContentRegistry& content);

    std::vector<SourceEntry> source_;
    std::vector<std::string> noAutoEggMobs_;
    std::vector<std::vector<Entry>> byMob_;
    std::vector<std::vector<Entry>> mergedByMob_;
    std::vector<std::string> unresolved_;
    std::vector<std::uint16_t> droppable_;
    std::uint16_t basicPetal_ = kNoPetal;   ///< the fallback when nothing is droppable
    const ContentRegistry* content_ = nullptr;
    std::uint32_t contentHash_ = 0;
    bool loaded_ = false;
};

// ---------------------------------------------------------------------------
// LootSystem
// ---------------------------------------------------------------------------

class LootSystem {
public:
    /// One petal that changed hands this tick. Applying it to the account is
    /// the runtime's job -- this system owns the world, not the database.
    struct Pickup {
        std::uint32_t quantity = 1;
        Entity player = NULL_ENTITY;
        std::uint16_t petalIndex = kNoPetal;
        Rarity rarity = Rarity::Common;
    };

    /// Wire ids for the drops this system creates. Null in a unit test; the
    /// runtime must point it at the server's one allocator or drops are
    /// invisible to every client while still being pickable up.
    NetIdAllocator* netIds = nullptr;

    /// Who ranks together when a corpse is shared out. Null means nobody is
    /// squadded, which is the ordinary case and the whole of what a unit test
    /// needs; see server/loot_eligibility.h for what a squad changes.
    const SquadEntityIndex* squads = nullptr;

    /// The tile world the per-tick pass pushes drops out of. Null in a unit
    /// test, where there is no map; the runtime MUST point it at the server's
    /// terrain or a drop scattered into a rock or into water stays there for
    /// its whole lifetime, visible and out of reach.
    const Terrain* terrain = nullptr;

    /// Called for each pickup as it happens, in addition to pickups(). Either
    /// is enough; the callback exists for a runtime that would rather not walk
    /// the list, the list for one that would rather not own a closure.
    std::function<void(const Pickup&)> onPickup;
    // A Rift prize must be credited to the account before its drop is consumed.
    std::function<bool(const Pickup&)> collectRiftPrize;
    std::function<int(Entity)> bonusMultiplier; // claimant-specific independent trials; Super+ uses 1.5x / 2x.

    /// Loads the shared JSON table during server startup. A missing or invalid
    /// file is fatal there instead of silently leaving every mob lootless.
    bool loadTables(const ContentRegistry& content, const std::string& path, std::string& errorOut) {
        return tables_.load(content, path, errorOut);
    }

    ServerWorkers* workers = nullptr;
    unsigned workerLimit = 0;

    void run(World& world, const SpatialGrid& grid, const ContentRegistry& content, Rng& rng,
             double nowMillis, double dt, CommandBuffer& commands, EventQueue& events);

    /// Called after the tick's pickup and expiry commands have drained, before
    /// replication can expose the ground. Returns the number of retired stacks.
    std::size_t pruneGroundDrops(World& world);

    /// Authored maps are immutable during a live server run. Other consumers
    /// may edit their terrain, and the daily maze always resolves its walls.
    bool immutableWorldTerrain = false;

    /// Everything picked up during the last run(). Cleared at the start of each.
    const std::vector<Pickup>& pickups() const { return pickups_; }

    /// Places one drop and returns it. `eligible` may be empty, which means
    /// anyone may take it immediately.
    Entity spawnDrop(World& world, std::uint16_t petalIndex, Rarity rarity, Vec2 position,
                     Realm realm, const std::vector<Entity>& eligible);

    /// Apply the whole drop rarity pipeline to one GRADED table row -- the
    /// one drop a kill is measured by. See pickFeatured for which row that is.
    static Rarity rollDropRarity(Rarity authoredRarity, Rarity mobRarity, Rng& rng);

    /// The first half of that pipeline: above uncommon, a 90% chance the row
    /// drops at one tier below the MOB instead of its authored rarity. Rolled
    /// once per graded row, upstream of the apex quantity loop, which is why
    /// it is separable at all -- ten apex copies share one base rarity.
    static Rarity scaleDropRarity(Rarity authoredRarity, Rarity mobRarity, Rng& rng);

    /// The second half: the mutually exclusive upgrade/downgrade roll, the
    /// mob's rarity floor and the apex item cap. Rolled per copy.
    static Rarity finishDropRarity(Rarity baseRarity, Rarity mobRarity, Rng& rng);

    /// What every row that is NOT the graded drop lands at: two tiers below
    /// the mob, flat. These are the rows guaranteed drops added on top of the
    /// one item a kill used to hand out, so they sit at the bottom of the
    /// mob's band where they cannot change how fast anyone progresses.
    static Rarity chaffDropRarity(Rarity mobRarity);

    /// Whether this flower may take a copy. `owner` is the connection behind
    /// the body (0 for a bot): a reservation follows the PLAYER, not the body
    /// they happened to be wearing when it was made, because a body does not
    /// survive its owner's death.
    static bool mayPickUp(const DropItem& drop, Entity player, net::ConnectionId owner);

private:
    void bind(World& world);
    void collectPickups(World& world, const SpatialGrid& grid, CommandBuffer& commands,
                        EventQueue& events);
    /// New and enlarged stacks can be picked up on the tick they are awarded.
    void collectFresh(World& world, CommandBuffer& commands, EventQueue& events);
    /// One flower against one drop: the shared body of both pickup passes.
    /// `playerRealm` is the flower's map, which the drop must be on too.
    void tryCollect(World& world, Entity player, Vec2 playerPosition, Realm playerRealm,
                    double reachSq, Entity candidate, CommandBuffer& commands,
                    EventQueue& events);
    /// Per-tick item maintenance, in the reference's order: wall push, bounds,
    /// expiry. The push is not a nicety -- nothing resolves the spawn scatter.
    void maintainDrops(double dt, CommandBuffer& commands);
    void awardDeaths(World& world, const ContentRegistry& content, Rng& rng, double nowMillis);
    // Per server and tier, independent of whether the Wave room is reopened.
    double nextWaveEternalLootMillis_ = 0.0;
    double nextWaveHyperLootMillis_ = 0.0;
    /// One full pass of a mob's table into `selected_`. The caller picks
    /// which table: the authored rows for a common mob, the merged one for
    /// every mob above it, since those drop one of everything they have.
    void rollTable(const std::vector<DropTables::Entry>& table, Rarity mobRarity, Rng& rng);
    /// Which row of `selected_` is the kill's graded drop, drawn weighted by
    /// `probability` exactly as the single pre-guaranteed-drops item was.
    /// Only meaningful above uncommon, where one item per kill is graded and
    /// the rest are chaff; below that every row is graded.
    std::size_t pickFeatured(Rng& rng) const;

    World* boundWorld_ = nullptr;
    std::optional<Query<PlayerTag, Transform, PlayerModifiers>> collectors_;
    std::optional<Query<DropTag, Transform, Lifetime, DropItem>> drops_;
    struct OverflowDrop {
        Entity entity;
        Realm realm;
        Rarity rarity;
        double remaining;
    };
    std::vector<OverflowDrop> groundDrops_;
    std::optional<Query<MobTag, Dead, MobType, Transform>> corpses_;

    DropTables tables_;
    std::vector<Pickup> pickups_;

    /// Reused every tick so the steady state does not allocate.
    std::vector<Entity> candidates_;
    struct DropMaintenanceBatch {
        const Entity* entities;
        Transform* transforms;
        Lifetime* lifetimes;
        DropItem* items;
        std::size_t size;
    };
    std::vector<DropMaintenanceBatch> maintenanceBatches_;
    std::vector<Entity> expired_;
    std::vector<Entity> fresh_;
    struct FreshPosition {
        Realm realm;
        Vec2 position;
        std::size_t order;
    };
    std::vector<FreshPosition> freshPositions_;
    std::vector<std::size_t> freshMatches_;
    std::vector<Entity> corpseList_;
    std::vector<Bounty::Share> ranked_;
    std::vector<Entity> eligible_;
    /// eligible_[i]'s worn orb tier, kRarityCount for none.
    std::vector<int> eligibleOrbs_;
    /// The eligible flowers sharing one orb tier: one split drop's claimants.
    std::vector<Entity> orbGroup_;
    std::vector<const DropTables::Entry*> selected_;
};

} // namespace flix

FLIX_COMPONENT(flix::LootAwarded);
