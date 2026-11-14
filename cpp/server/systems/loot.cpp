#include "server/systems/loot.h"
#include "server/workers.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <tuple>
#include <map>

#include "server/loot_eligibility.h"
#include "shared/core/json.h"
#include "shared/game/garbage_contents.h"

namespace flix {

// ---------------------------------------------------------------------------
// DropTables
// ---------------------------------------------------------------------------

bool DropTables::linkedTo(const ContentRegistry& content) const {
    return loaded_ && content_ == &content && contentHash_ == content.contentHash();
}

bool DropTables::load(const ContentRegistry& content, const std::string& path, std::string& errorOut) {
    Json root;
    std::string parseError;
    if (!Json::parseFile(path, root, parseError)) {
        errorOut = path + ": " + parseError;
        return false;
    }
    if (!root.isObject()) {
        errorOut = path + ": top level is not an object";
        return false;
    }

    std::vector<SourceEntry> loaded;
    std::vector<std::string> noAutoEgg;
    for (const std::string& mobId : root.keys()) {
        const Json& table = root[mobId];
        if(table.contains("autoEggDrops")&&!table["autoEggDrops"].asBool(true))noAutoEgg.push_back(mobId);
        if (!table.isObject() || !table["drops"].isArray()) {
            errorOut = path + ": table for '" + mobId + "' must contain a drops array";
            return false;
        }

        for (const Json& entry : table["drops"].items()) {
            if (!entry.isObject()) {
                errorOut = path + ": a drop for '" + mobId + "' is not an object";
                return false;
            }
            const std::string type = entry["type"].asString();
            const std::string itemId = entry["itemType"].asString();
            const std::string rarity = entry["rarity"].asString();
            if (itemId.empty() || rarity.empty() || !entry["probability"].isNumber()) {
                errorOut = path + ": a drop for '" + mobId + "' is missing itemType, rarity, or probability";
                return false;
            }

            // Ladder tiers only: nothing drops universal, so a table naming
            // it is refused like any other unknown tier.
            int rarityOffset = -1;
            for (int i = 0; i < kLadderRarityCount; ++i) {
                if (rarity == kRarityNames[static_cast<std::size_t>(i)]) {
                    rarityOffset = i;
                    break;
                }
            }
            if (rarityOffset < 0) {
                errorOut = path + ": drop '" + itemId + "' for '" + mobId + "' has an unknown rarity";
                return false;
            }

            // Mobs do not drop consumables: a `consumable` row is not part of
            // the mob's table at all.
            if (type == "consumable") continue;

            SourceEntry source;
            source.mobId = mobId;
            source.petalId = itemId;
            // Anything else is a petal, which is how the reference reads the
            // field. Every row is kept whatever its kind: the row list is what
            // the mob HAS, and above common a mob leaves one of everything it
            // has.
            source.kind = itemId == "random" ? Kind::RandomPetal : Kind::Petal;
            source.rarityOffset = rarityOffset;
            source.probability = clamp(entry["probability"].asDouble(), 0.0, 1.0);
            source.minCount = std::max(1, entry["minQuantity"].asInt(1));
            source.maxCount = std::max(source.minCount, entry["maxQuantity"].asInt(source.minCount));
            if(entry.contains("rarityChances")){
                if(content.mobIndex(mobId)==kInvalidIndex||content.petalIndex(itemId)==kInvalidIndex||
                   !parseDropRates(entry["rarityChances"],source.rates)){
                    errorOut=path+": invalid explicit drop rates for "+mobId+"/"+itemId;return false;
                }
                source.rollGroup=entry["rollGroup"].asInt(0);
                if(source.rollGroup<0||source.rollGroup>1){errorOut="invalid drop roll group";return false;}
                if(entry.contains("magicItemType")){
                    source.magicPetalId=entry["magicItemType"].asString();
                    if(content.petalIndex(source.magicPetalId)==kInvalidIndex){errorOut="invalid magic drop petal";return false;}
                }
                if(entry.contains("magicRarityChances")){
                    if(source.magicPetalId.empty()||!parseDropRates(entry["magicRarityChances"],source.magicRates)){
                        errorOut=path+": invalid conditional magic rates for "+mobId;return false;
                    }
                }
            }
            loaded.push_back(std::move(source));
        }
    }

    source_ = std::move(loaded);
    noAutoEggMobs_=std::move(noAutoEgg);
    loaded_ = true;
    content_ = nullptr;
    contentHash_ = 0;
    resolve(content);
    errorOut.clear();
    return true;
}

void DropTables::loadDefault(const ContentRegistry& content) {
    // Relative to the working directory. From a build directory the first is
    // the staged copy; from the repository root it is the source, and so are
    // the other two from cpp/ and from cpp/build.
    static constexpr const char* kCandidates[] = {
        "data/mob_drops.json",
        "../data/mob_drops.json",
        "../../data/mob_drops.json",
    };

    for (const char* candidate : kCandidates) {
        std::ifstream probe(candidate, std::ios::binary);
        if (!probe) continue;
        std::string ignored;
        if (load(content, candidate, ignored)) return;
    }

    // A standalone system test can have no data directory at all. Mark the
    // attempt so the steady-state tick does not repeatedly hit the filesystem.
    loaded_ = true;
    resolve(content);
}

void DropTables::link(const ContentRegistry& content) {
    if (!loaded_) loadDefault(content);
    if (linkedTo(content)) return;
    resolve(content);
}

void DropTables::resolve(const ContentRegistry& content) {
    content_ = &content;
    contentHash_ = content.contentHash();

    byMob_.assign(content.mobCount(), std::vector<Entry>{});
    unresolved_.clear();

    for (const SourceEntry& source : source_) {
        const std::uint16_t mobIndex = content.mobIndex(source.mobId);
        if (mobIndex == kInvalidIndex || mobIndex >= byMob_.size()) {
            unresolved_.push_back(std::string("mob '") + source.mobId +
                                  "' has a drop table but no config");
            continue;
        }

        Entry resolved;
        // A row whose item this build cannot hand out -- an id the petal
        // registry does not know -- still belongs in the table: on
        // a common mob it is an independent roll and above one it is a
        // guaranteed drop. Only the payout is missing, and kNoPetal is already
        // what spawnDrop treats as "nothing". (The `random` sentinel keeps
        // kNoPetal here too, and is resolved per copy in awardDeaths.)
        resolved.kind = source.kind;
        resolved.petalIndex =
            source.kind == Kind::Petal ? content.petalIndex(source.petalId) : kNoPetal;
        resolved.rarityOffset = source.rarityOffset;
        resolved.probability = source.probability;
        resolved.minCount = source.minCount;
        resolved.maxCount = source.maxCount;
        resolved.rates=source.rates;
        resolved.magicRates=source.magicRates;
        resolved.magicPetal=content.petalIndex(source.magicPetalId);
        resolved.rollGroup=source.rollGroup;
        byMob_[mobIndex].push_back(resolved);
    }

    // The TypeScript registry auto-generates a guaranteed common egg row for
    // every non-pet mob that permits eggs. It is runtime content, so it is not
    // present in mob_drops.json and must be restored after linking.
    for (std::size_t i = 0; i < content.mobCount(); ++i) {
        const MobConfig& mob = content.mob(static_cast<std::uint16_t>(i));
        if(std::find(noAutoEggMobs_.begin(),noAutoEggMobs_.end(),mob.id)!=noAutoEggMobs_.end())continue;
        if (mob.noEggDrop ||
            (mob.id.size() >= 4 && mob.id.compare(mob.id.size() - 4, 4, "_pet") == 0)) continue;
        const std::uint16_t egg = content.petalIndex(mob.id + "_egg");
        if (egg == kInvalidIndex) continue;
        auto& rows = byMob_[i];
        auto existing = std::find_if(rows.begin(), rows.end(), [&](const Entry& row) {
            return row.kind == Kind::Petal && row.petalIndex == egg &&
                   row.rarityOffset == rarityIndex(Rarity::Common);
        });
        if (existing != rows.end()) {
            existing->probability = 1.0;
        } else {
            Entry row;
            row.petalIndex = egg;
            row.rarityOffset = rarityIndex(Rarity::Common);
            row.probability = 1.0;
            rows.insert(rows.begin(), row);
        }
    }

    // Above common every row pays out, so the authored habit of giving one
    // petal two lines -- a common rose beside an uncommon one -- would leave
    // two roses on the ground per kill. They are one drop TYPE, so they fold
    // into one row here, once, rather than per corpse. Built after the egg
    // pass above so a hand-authored egg row cannot come back as a second egg.
    mergedByMob_.assign(byMob_.size(), std::vector<Entry>{});
    for (std::size_t i = 0; i < byMob_.size(); ++i) {
        std::vector<Entry>& merged = mergedByMob_[i];
        for (const Entry& row : byMob_[i]) {
            // Only a NAMED petal can collide. The `random` sentinel and an
            // unknown id both resolve to kNoPetal, and folding those together
            // would merge two unrelated rows.
            Entry* into = nullptr;
            if (row.kind == Kind::Petal && row.petalIndex != kNoPetal) {
                for (Entry& candidate : merged) {
                    if(!row.rates.empty()||!candidate.rates.empty())continue;
                    if (candidate.kind == row.kind && candidate.petalIndex == row.petalIndex) {
                        into = &candidate;
                        break;
                    }
                }
            }
            if (into == nullptr) {
                merged.push_back(row);
                continue;
            }
            // The chance either authored line would have fired, which is what
            // those two lines meant together before the merge.
            into->probability = 1.0 - (1.0 - into->probability) * (1.0 - row.probability);
            into->rarityOffset = std::max(into->rarityOffset, row.rarityOffset);
            into->minCount = std::max(into->minCount, row.minCount);
            into->maxCount = std::max(into->maxCount, row.maxCount);
        }
    }

    // What a `random` row may turn into. A property of the content rather than
    // of a kill, so it is derived here once: admin petals, the two cutters and
    // the eggs of mobs that never lay one are excluded however they are
    // authored, and every other petal is equally likely.
    droppable_.clear();
    for (const std::uint16_t index : content.petalDisplayOrder()) {
        const PetalConfig& petal = content.petal(index);
        if (petal.isAdminPetal) continue;
        if (petal.id == "cutter" || petal.id == "lightning_cutter") continue;
        // Handed out by a rule of their own: the sacrifice, the crown.
        if (petalHasOwnSource(petal)) continue;
        // A magic petal is CONVERTED into, never dropped. Leaving it in the
        // pool would put an apex magic leaf in front of a flower wearing a
        // common orb, straight past the gate the orb exists to be.
        if (content.isMagicForm(index)) continue;
        if (petal.id.size() > 4 && petal.id.compare(petal.id.size() - 4, 4, "_egg") == 0) {
            const std::uint16_t layer = content.mobIndex(petal.id.substr(0, petal.id.size() - 4));
            if (layer != kInvalidIndex && content.mob(layer).noEggDrop) continue;
        }
        droppable_.push_back(index);
    }
    basicPetal_ = content.petalIndex("basic");
}

const std::vector<DropTables::Entry>& DropTables::forMob(std::uint16_t mobIndex) const {
    static const std::vector<Entry> kNothing;
    if (mobIndex >= byMob_.size()) return kNothing;
    return byMob_[mobIndex];
}

const std::vector<DropTables::Entry>& DropTables::guaranteedForMob(std::uint16_t mobIndex) const {
    static const std::vector<Entry> kNothing;
    if (mobIndex >= mergedByMob_.size()) return kNothing;
    return mergedByMob_[mobIndex];
}

std::uint16_t DropTables::randomPetal(Rng& rng, Rarity rarity) const {
    if (droppable_.empty()) return basicPetal_;
    // A handful of draws, then the basic petal: with one petal in the pool
    // held above common the first draw all but always stands.
    for (int draw = 0; draw < 8; ++draw) {
        const std::uint16_t index =
            droppable_[rng.below(static_cast<std::uint32_t>(droppable_.size()))];
        if (content_ == nullptr || petalComesAt(content_->petal(index), rarity)) return index;
    }
    return basicPetal_;
}

// ---------------------------------------------------------------------------
// Rolls
// ---------------------------------------------------------------------------

Rarity LootSystem::scaleDropRarity(Rarity authoredRarity, Rarity mobRarity, Rng& rng) {
    // Above uncommon, 90% of graded rows first become one tier below the
    // mob; common and uncommon rows keep their authored table rarity.
    if (rarityIndex(mobRarity) > rarityIndex(Rarity::Uncommon) && rng.chance(0.9)) {
        return clampRarity(rarityIndex(mobRarity) - 1);
    }
    return authoredRarity;
}

Rarity LootSystem::chaffDropRarity(Rarity mobRarity) {
    // Flat, and deliberately not run through finishDropRarity: the rarity
    // floor there exists to hold the mob's ONE graded drop up near its tier,
    // and applying it to the extra rows would undo the whole point of them
    // being chaff -- a rare mob's floor would lift every one of them back to
    // uncommon.
    return clampRarity(rarityIndex(mobRarity) - 2);
}

Rarity LootSystem::finishDropRarity(Rarity baseRarity, Rarity mobRarity, Rng& rng) {
    Rarity base = baseRarity;
    const double upgrade =
        clamp(dropUpgradeChance(base) * dropUpgradeMultiplier(mobRarity), 0.0, 1.0);
    if (rng.chance(upgrade)) {
        base = upgradeRarity(base);
    } else if (rng.chance(dropDowngradeChance(base))) {
        base = downgradeRarity(base);
    }

    // Rare mobs floor at tier-1; epic and above floor at tier-2.
    const int mobTier = rarityIndex(mobRarity);
    if (mobTier >= rarityIndex(Rarity::Rare)) {
        const int floor = mobTier >= rarityIndex(Rarity::Epic) ? mobTier - 2 : mobTier - 1;
        if (rarityIndex(base) < floor) base = clampRarity(floor);
    }
    // Apex mobs explicitly cap item rarity at unique.
    if (mobRarity == Rarity::Apex && base == Rarity::Apex) base = Rarity::Unique;
    return base;
}

Rarity LootSystem::rollDropRarity(Rarity authoredRarity, Rarity mobRarity, Rng& rng) {
    return finishDropRarity(scaleDropRarity(authoredRarity, mobRarity, rng), mobRarity, rng);
}

bool LootSystem::mayPickUp(const DropItem& drop, Entity player, net::ConnectionId owner) {
    // By the OWNER where there is one, so a player who died between the kill
    // and the walk back still collects what was reserved for them -- and
    // still cannot collect it twice by dying again. See LootClaim.
    if (claimed(drop.pickedUpBy, player, owner)) return false;
    // Gone to the first taker, even before the destroy is flushed: two
    // flowers reaching it on the same tick do not both get it.
    if (drop.singleCopy && !drop.pickedUpBy.empty()) return false;
    return drop.eligible.empty() || claimed(drop.eligible, player, owner);
}

// ---------------------------------------------------------------------------
// The magic orb's conversion
// ---------------------------------------------------------------------------

namespace {

/// The best magic orb on a flower's ACTIVE bar, or kRarityCount for none.
///
/// Active slots only, for the reason every other worn bonus reads only those:
/// a stashed orb is not a worn one. The BEST of several rather than the first,
/// because two orbs are one conversion, at the better tier.
int wornOrbTier(const World& world, const ContentRegistry& content, Entity player,
                std::uint16_t orbIndex) {
    if (orbIndex == kInvalidIndex) return kRarityCount;
    const Loadout* loadout = world.tryGet<Loadout>(player);
    if (loadout == nullptr) return kRarityCount;
    int best = kRarityCount;
    for (int i = 0; i < kLoadoutActiveSlots; ++i) {
        // Resolved: a mimic beside an orb is an orb at the mimic's tier.
        const EquippedPetal slot = equippedPetal(content, *loadout, i);
        if (slot.empty() || slot.configIndex != orbIndex) continue;
        const int tier = rarityIndex(slot.rarity);
        if (best == kRarityCount || tier > best) best = tier;
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------
// Drops
// ---------------------------------------------------------------------------

Entity LootSystem::spawnDrop(World& world, std::uint16_t petalIndex, Rarity rarity, Vec2 position,
                             Realm realm, const std::vector<Entity>& eligible) {
    if (petalIndex == kNoPetal) return NULL_ENTITY;

    const Entity e = world.create();
    world.add<DropTag>(e);
    world.add<Transform>(e, Transform{position, 0.0, realm});
    // No Motion: a drop is furniture. The body is here only so the broadphase
    // files it and the pickup query can find it.
    world.add<Body>(e, Body{kDropBodyRadius, 1.0});

    DropItem item;
    item.configIndex = petalIndex;
    item.rarity = rarity;
    // Resolved to CLAIMS here, where the world is in hand: the ranking that
    // produced this list deals in bodies, and a body is not who a drop
    // belongs to.
    item.eligible.clear();
    item.eligible.reserve(eligible.size());
    for (const Entity claimant : eligible) {
        const PlayerAccount* account = world.tryGet<PlayerAccount>(claimant);
        item.eligible.push_back(
            LootClaim{claimant, account != nullptr ? account->connection : 0});
    }
    world.add<DropItem>(e, std::move(item));

    world.add<Lifetime>(e, Lifetime{kDropLifetimeByRarity[static_cast<std::size_t>(rarityIndex(rarity))]});
    world.add<Replicated>(e, Replicated{net::EntityKind::Drop, 0, petalIndex, rarity, 0});
    if (netIds != nullptr) world.add<NetId>(e, NetId{netIds->next()});
    return e;
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------

namespace {

/// How far a flower reaches for loot.
///
/// Magnetism widens the reach and nothing else. Pulling the drop in would
/// consume it before a snapshot ever carried it.
double pickupReach(const World& world, Entity player, const PlayerModifiers& mods) {
    const Body* body = world.tryGet<Body>(player);
    const double base = body != nullptr ? body->radius * 2.0 : kDropPickupRadius;
    return base + std::max(0.0, mods.magnetism);
}

} // namespace

void LootSystem::bind(World& world) {
    if (boundWorld_ == &world) return;
    boundWorld_ = &world;
    collectors_.emplace(world);
    collectors_->without<Dead>();
    drops_.emplace(world);
    corpses_.emplace(world);
    // A pet dying is its owner's petal breaking, not a kill, and a corpse that
    // has already paid out must never be walked again.
    corpses_->without<LootAwarded, Pet>();
}

void LootSystem::run(World& world, const SpatialGrid& grid, const ContentRegistry& content,
                     Rng& rng, double nowMillis, double dt, CommandBuffer& commands,
                     EventQueue& events) {
    bind(world);
    tables_.link(content);
    pickups_.clear();

    maintainDrops(dt, commands);
    collectPickups(world, grid, commands, events);
    awardDeaths(world, content, rng, nowMillis);
    // Deaths pay out after the broadphase pass, but their loot is still
    // collectable this tick: the reference rolls a mob's drops inside the very
    // player step that then tests pickups, so a magnet flower standing on its
    // own kill takes the item before any snapshot could have carried it. The
    // client is not left with nothing to animate -- the pickup cue carries the
    // drop's position and look for exactly this case.
    collectFresh(world, commands, events);
}

std::size_t LootSystem::pruneGroundDrops(World& world) {
    bind(world);
    if (drops_->count() <= kMaxGroundDropsPerRealm) return 0;

    std::array<std::size_t, kMaxRealms> byRealm{};
    groundDrops_.clear();
    drops_->each([&](Entity e, DropTag&, Transform& at, Lifetime& life, DropItem& drop) {
        if (drop.singleCopy || drop.humansOnly) return;
        ++byRealm[realmIndex(at.realm)];
        groundDrops_.push_back({e, at.realm, drop.rarity, std::max(0.0, life.remainingSeconds)});
    });
    const auto initial = groundDrops_.size();
    if (initial <= kMaxGroundDrops &&
        std::none_of(byRealm.begin(), byRealm.end(),
                     [](std::size_t count) { return count > kMaxGroundDropsPerRealm; })) return 0;

    std::sort(groundDrops_.begin(), groundDrops_.end(), [](const OverflowDrop& a, const OverflowDrop& b) {
        if (a.rarity != b.rarity) return rarityIndex(a.rarity) < rarityIndex(b.rarity);
        if (a.remaining != b.remaining) return a.remaining < b.remaining;
        return a.entity < b.entity;
    });
    std::size_t retained = initial;
    for (auto& drop : groundDrops_) {
        auto& count = byRealm[realmIndex(drop.realm)];
        if (count <= kMaxGroundDropsPerRealm) continue;
        world.destroy(drop.entity);
        drop.entity = NULL_ENTITY;
        --count;
        --retained;
    }
    for (const auto& drop : groundDrops_) {
        if (retained <= kMaxGroundDrops) break;
        if (drop.entity == NULL_ENTITY) continue;
        world.destroy(drop.entity);
        --retained;
    }
    return initial - retained;
}

void LootSystem::maintainDrops(double dt, CommandBuffer& commands) {
    const auto expires = [&](Transform& transform, Lifetime& lifetime, DropItem& drop) {
        // Nothing resolves the +-50 spawn scatter -- the reference does not
        // either, by design -- so this push is the only way a drop that landed
        // inside a rock, a wall or water ever becomes reachable again. Pickup
        // is a plain distance test, and a tile face is far wider than its reach.
        lifetime.remainingSeconds -= dt;
        if (terrain == nullptr) return lifetime.remainingSeconds <= 0.0;
        const bool immutable = immutableWorldTerrain && isWorldRealm(transform.realm);
        const Vec2 before = transform.position;
        if (immutable && drop.terrainSettled && drop.settledRealm == transform.realm &&
            drop.settledPosition.x == before.x && drop.settledPosition.y == before.y) {
            return lifetime.remainingSeconds <= 0.0;
        }
        transform.position = terrain->resolveCircle(before, kDropWallRadius, transform.realm);
        const bool outside = terrain->outside(transform.position, transform.realm);
        // A fixed point of the static wall solver is safe to reuse. A push
        // gets checked again, including cases requiring more than one tick.
        drop.terrainSettled = immutable && !outside &&
                              transform.position.x == before.x && transform.position.y == before.y;
        if (drop.terrainSettled) {
            drop.settledPosition = before;
            drop.settledRealm = transform.realm;
        }
        return lifetime.remainingSeconds <= 0.0 || outside;
    };
    if (!workers || !workers->count() || !workerLimit || !terrain || drops_->count() < 256) {
        drops_->each([&](Entity e, DropTag&, Transform& transform, Lifetime& lifetime, DropItem& item) {
            if (expires(transform, lifetime, item)) commands.destroy(e);
        });
        return;
    }

    constexpr std::size_t batchSize = 64;
    maintenanceBatches_.clear();
    drops_->chunks([&](std::size_t size, const Entity* entities, DropTag*, Transform* transforms,
                       Lifetime* lifetimes, DropItem* items) {
        for (std::size_t i = 0; i < size; i += batchSize) {
            maintenanceBatches_.push_back({entities + i, transforms + i, lifetimes + i,
                                           items + i, std::min(batchSize, size - i)});
        }
    });
    expired_.resize(maintenanceBatches_.size() * batchSize);
    workers->run(maintenanceBatches_.size(), workerLimit, [&](std::size_t index, unsigned) {
        const auto& batch = maintenanceBatches_[index];
        Entity* results = expired_.data() + index * batchSize;
        for (std::size_t i = 0; i < batchSize; ++i) {
            results[i] = i < batch.size && expires(batch.transforms[i], batch.lifetimes[i], batch.items[i])
                ? batch.entities[i] : NULL_ENTITY;
        }
    });
    for (const Entity e : expired_) {
        if (e != NULL_ENTITY) commands.destroy(e);
    }
}

void LootSystem::collectPickups(World& world, const SpatialGrid& grid, CommandBuffer& commands,
                                EventQueue& events) {
    collectors_->each([&](Entity player, PlayerTag&, Transform& transform, PlayerModifiers& mods) {
        const Health* health = world.tryGet<Health>(player);
        if (health != nullptr && !health->alive()) return;

        const double reach = pickupReach(world, player, mods);
        grid.query(transform.realm, transform.position, reach, candidates_);
        for (const Entity candidate : candidates_) {
            tryCollect(world, player, transform.position, transform.realm, reach * reach,
                       candidate, commands, events);
        }
    });
}

void LootSystem::collectFresh(World& world, CommandBuffer& commands, EventQueue& events) {
    if (fresh_.empty()) return;
    // Small batches are cheaper to walk directly. For a burst, index once
    // and let each player visit only the drops in their realm and reach.
    const bool indexed = fresh_.size() > 32 && collectors_->count() > 1;
    const auto key = [](const FreshPosition& drop) {
        return std::pair{drop.realm, drop.position.x};
    };
    if (indexed) {
        freshPositions_.clear();
        for (std::size_t i = 0; i < fresh_.size(); ++i) {
            const Transform& at = world.get<Transform>(fresh_[i]);
            freshPositions_.push_back({at.realm, at.position, i});
        }
        std::sort(freshPositions_.begin(), freshPositions_.end(),
                  [&](const FreshPosition& a, const FreshPosition& b) { return key(a) < key(b); });
    }
    collectors_->each([&](Entity player, PlayerTag&, Transform& transform, PlayerModifiers& mods) {
        const Health* health = world.tryGet<Health>(player);
        if (health != nullptr && !health->alive()) return;

        const double reach = pickupReach(world, player, mods);
        const auto collect = [&](Entity candidate) {
            tryCollect(world, player, transform.position, transform.realm, reach * reach,
                       candidate, commands, events);
        };
        if (!indexed) {
            for (Entity candidate : fresh_) collect(candidate);
            return;
        }
        const auto left = std::pair{transform.realm, transform.position.x - reach};
        const auto right = std::pair{transform.realm, transform.position.x + reach};
        auto first = std::lower_bound(freshPositions_.begin(), freshPositions_.end(), left,
            [&](const FreshPosition& drop, const auto& bound) { return key(drop) < bound; });
        freshMatches_.clear();
        for (; first != freshPositions_.end() && key(*first) <= right; ++first) {
            if (std::abs(first->position.y - transform.position.y) <= reach) {
                freshMatches_.push_back(first->order);
            }
        }
        // Keep the pickup and event order of the original player-first walk.
        std::sort(freshMatches_.begin(), freshMatches_.end());
        for (std::size_t i : freshMatches_) collect(fresh_[i]);
    });
}

void LootSystem::tryCollect(World& world, Entity player, Vec2 playerPosition, Realm playerRealm,
                            double reachSq, Entity candidate, CommandBuffer& commands,
                            EventQueue& events) {
    DropItem* drop = world.tryGet<DropItem>(candidate);
    if (drop == nullptr) return;
    const Transform* at = world.tryGet<Transform>(candidate);
    if (at == nullptr) return;
    // A drop on another map is out of reach whatever the numbers say. The
    // broadphase pass only ever finds drops filed under the flower's realm,
    // but the fresh pass walks this tick's drops directly -- so without this
    // a flower standing at the same coordinates on another map, a contributor
    // who changed map on the tick of the kill, took its copy from there.
    if (at->realm != playerRealm) return;
    if (distanceSq(at->position, playerPosition) > reachSq) return;
    const PlayerAccount* taker = world.tryGet<PlayerAccount>(player);
    const net::ConnectionId owner = taker != nullptr ? taker->connection : 0;
    // A bot's body carries no connection; see DropItem::humansOnly.
    if (drop->humansOnly && owner == 0) return;
    if (!mayPickUp(*drop, player, owner)) return;
    if (drop->quantity == 0 || drop->quantity > kMaxStackCount) {
        std::fprintf(stderr, "[loot] rejected invalid ground stack: petal=%u tier=%u quantity=%u\n",
                     static_cast<unsigned>(drop->configIndex),
                     static_cast<unsigned>(rarityIndex(drop->rarity)),
                     static_cast<unsigned>(drop->quantity));
        commands.destroy(candidate);
        return;
    }

    const Pickup pickup{drop->quantity, player, drop->configIndex, drop->rarity};
    if (drop->riftPrize) {
        if (!collectRiftPrize || !collectRiftPrize(pickup)) return;
    } else {
        pickups_.push_back(pickup);
        if (onPickup) onPickup(pickup);
    }

    const NetId* dropId = world.tryGet<NetId>(candidate);
    const NetId* playerId = world.tryGet<NetId>(player);
    if (dropId != nullptr && playerId != nullptr) {
        events.pickedUp(dropId->value, playerId->value, at->position, at->realm,
                        drop->configIndex, drop->rarity, drop->quantity, owner);
    }
    drop->pickedUpBy.push_back(LootClaim{player, owner});
    bool finished = drop->singleCopy;
    if (!finished && !drop->eligible.empty()) {
        finished = std::all_of(drop->eligible.begin(), drop->eligible.end(),
                               [&](const LootClaim& claim) {
                                   return claimed(drop->pickedUpBy, claim.body, claim.owner);
                               });
    }
    if (finished) commands.destroy(candidate);
}

void LootSystem::awardDeaths(World& world, const ContentRegistry& content, Rng& rng, double nowMillis) {
    fresh_.clear();
    corpses_->collect(corpseList_);
    if (corpseList_.empty()) return;
    // All drops from a death stay in the corpse's realm. Busy farming maps
    // should not rebuild the stack index for every other map on each kill.
    std::array<bool, kMaxRealms> deathRealms{};
    for (const Entity corpse : corpseList_) {
        if (const auto* at = world.tryGet<Transform>(corpse)) {
            deathRealms[realmIndex(at->realm)] = true;
        }
    }
    // Merge nearby unclaimed stacks only. Once anybody has collected a
    // share, the remaining players keep that exact original entitlement.
    // Search only nearby stacks of the same item and realm, instead of
    // scanning every drop in the world for every individual loot roll.
    using StackKey = std::tuple<unsigned, std::uint16_t, int, int, int>;
    struct StackEntry {
        std::size_t order;
        Entity entity;
        bool fresh;
    };
    std::map<StackKey, std::vector<StackEntry>> stackable;
    std::size_t stackOrder = 0;
    const auto indexStack = [&](Entity e, const DropItem& item, const Transform& at, bool fresh) {
        stackable[{static_cast<unsigned>(at.realm),item.configIndex,rarityIndex(item.rarity),
            static_cast<int>(std::floor(at.position.x/150.0)),
            static_cast<int>(std::floor(at.position.y/150.0))}].push_back({stackOrder++, e, fresh});
    };
    Query<DropTag, DropItem, Transform> stackQuery{world};
    stackQuery.each([&](Entity e, DropTag&, DropItem& item, Transform& at) {
        if (!deathRealms[realmIndex(at.realm)]) return;
        const auto* life = world.tryGet<Lifetime>(e);
        if (!item.singleCopy && !item.humansOnly && item.pickedUpBy.empty() &&
            life && life->remainingSeconds > 0) indexStack(e, item, at, false);
    });
    bool hyperKill=false,hyperEternalRoll=false;
    std::vector<Entity> reducedClaimants;
    std::vector<Entity> filteredClaimants;
    const auto stackDrop = [&](std::uint16_t petal, Rarity rarity, Vec2 at, Realm realm,
                               const std::vector<Entity>& claimants) {
        rarity=payoutDropRarity(rarity,hyperKill?Rarity::Apex:Rarity::Common);
        if(hyperKill&&rarity==Rarity::Unique&&!hyperEternalRoll)return;
        if (petal == kNoPetal) return;
        // Thin the weak shares after rolling. This also halves special
        // guaranteed copies, while preserving the shared loot roll.
        if (!reducedClaimants.empty()) {
            filteredClaimants.clear();
            for (Entity claimant : claimants) {
                if (std::find(reducedClaimants.begin(), reducedClaimants.end(), claimant) !=
                        reducedClaimants.end() &&
                    !rng.chance(kHyperLowRewardTenths / 10.0)) continue;
                filteredClaimants.push_back(claimant);
            }
        }
        const auto& eligible = reducedClaimants.empty() ? claimants : filteredClaimants;
        if (eligible.empty()) return;
        StackEntry* merge = nullptr;
        std::size_t first = stackOrder;
        const int cellX=static_cast<int>(std::floor(at.x/150.0));
        const int cellY=static_cast<int>(std::floor(at.y/150.0));
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy) {
          const auto bucket=stackable.find({static_cast<unsigned>(realm),petal,rarityIndex(rarity),cellX+dx,cellY+dy});
          if(bucket==stackable.end())continue;
          for (auto& entry : bucket->second) {
            if (entry.order >= first) break;
            const Entity e = entry.entity;
            auto& item = world.get<DropItem>(e);
            const auto& position = world.get<Transform>(e);
             if (item.configIndex != petal || item.rarity != rarity || position.realm != realm ||
                 item.quantity >= kMaxStackCount ||
                distanceSq(position.position, at) > 150.0 * 150.0 ||
                item.eligible.size() != eligible.size()) continue;
            bool same = true;
            for (Entity player : eligible) {
                const auto* account = world.tryGet<PlayerAccount>(player);
                if (!claimed(item.eligible, player, account ? account->connection : 0)) same = false;
            }
            if (!same) continue;
            merge = &entry;
            first = entry.order;
            break;
          }
        }
        // Preserve the old first-match ordering even across cell boundaries.
        if (merge != nullptr) {
            const Entity e = merge->entity;
            auto& item=world.get<DropItem>(e);
            ++item.quantity;
            world.get<Lifetime>(e).remainingSeconds =
                kDropLifetimeByRarity[static_cast<std::size_t>(rarityIndex(rarity))];
            if (!merge->fresh) {
                fresh_.push_back(e);
                merge->fresh = true;
            }
            return;
        }
        const Entity e = spawnDrop(world, petal, rarity, at, realm, eligible);
        fresh_.push_back(e);
        indexStack(e, world.get<DropItem>(e), world.get<Transform>(e), true);
    };
    // Once for the tick, not once per corpse: the conversion below asks for it
    // on every drop of every kill.
    const auto legacyOrb = content.petalIndex("magic_orb");
    const std::uint16_t magicOrb = legacyOrb!=kInvalidIndex?legacyOrb:content.petalIndex("mana_orb");
    for (const Entity corpse : corpseList_) {
        const MobType* type = world.tryGet<MobType>(corpse);
        const Transform* transform = world.tryGet<Transform>(corpse);
        if (type == nullptr || transform == nullptr) continue;
        if (const auto* subdivision = world.tryGet<CancerSubdivision>(corpse);
            subdivision && subdivision->spawnedCopy) {
            world.add<LootAwarded>(corpse);
            continue;
        }
        const auto* wave = world.tryGet<WaveEnemy>(corpse);
        const bool waveBoss = wave && (type->rarity == Rarity::Unique || type->rarity == Rarity::Apex);
        double& waveReady = type->rarity == Rarity::Apex ? nextWaveHyperLootMillis_ : nextWaveEternalLootMillis_;
        if (wave && (!wave->canDrop || (waveBoss && nowMillis < waveReady))) {
            world.add<LootAwarded>(corpse);
            continue;
        }
        const bool apexKill = type->rarity == Rarity::Ascendant;
        hyperKill = type->rarity == Rarity::Apex || apexKill;
        reducedClaimants.clear();

        // Read the corpse out in full first. Everything below is a structural
        // change, and these pointers are into an archetype column. The realm
        // too: adding LootAwarded moves the corpse out of its row, and the
        // corpse swapped into that row may be standing on another map.
        const std::uint16_t mobIndex = type->configIndex;
        const Rarity mobRarity = apexKill ? Rarity::Apex : type->rarity;
        const Vec2 at = transform->position;
        const Realm realm = transform->realm;
        const Health* health = world.tryGet<Health>(corpse);
        const double maxHealth = health != nullptr ? health->max : 0.0;
        const double damageFloor = apexKill ? lootDamageFloor(maxHealth) :
                                                    lootDamageFloor(mobRarity, maxHealth);

        // Loot slots go to the highest positive contributors, just like XP.
        ranked_.clear();
        eligible_.clear();
        if (const Bounty* bounty = world.tryGet<Bounty>(corpse)) {
            for (const Bounty::Share& share : bounty->contributors) {
                if (share.damage <= 0.0) continue;
                // Refresh the body across death / title rejoin (connection key).
                // A contributor whose session is GONE still burns one of the
                // tier's slots: nobody is promoted into the gap. Only a live
                // entity that is not a flower is refused.
                const Bounty::Share live = refreshedShare(world, share);
                if (world.isAlive(live.player) && !world.has<PlayerTag>(live.player)) continue;
                ranked_.push_back(live);
            }
            // Stable: contributors are stored in first-hit order and the
            // reference's sort is specified stable, so on an exact damage tie
            // the slot at the cut belongs to whoever landed their damage first.
            // The order matters beyond the cut too -- the credit fallback below
            // reads the top of this list.
            std::stable_sort(ranked_.begin(), ranked_.end(), [](const auto& a, const auto& b) {
                return a.damage > b.damage;
            });
            selectLootRecipients(ranked_, lootSlotsForRarity(mobRarity), squads, eligible_,
                                 damageFloor);
        }

        if (const auto* inherited = world.tryGet<InheritedLootClaims>(corpse)) {
            for (const auto& claim : inherited->recipients) {
                const Entity player = liveContributorBody(world, {claim.player, claim.owner, 0.0});
                if (player != NULL_ENTITY &&
                    std::find(eligible_.begin(), eligible_.end(), player) == eligible_.end())
                    eligible_.push_back(player);
            }
        }

        // A common mob rolls its authored rows one by one; everything above
        // it leaves one of every drop it has, from the merged table.
        std::vector<DropTables::Entry> garbageTable;
        const bool visibleGarbage = content.mob(mobIndex).id == "garbage" && !content.garbageDropRates().empty();
        if (visibleGarbage) {
            const auto* id = world.tryGet<NetId>(corpse);
            const auto contents = garbageContents(garbageEligiblePetals(content,mobRarity),id ? id->value : 0,at,&content);
            for (const auto& item : contents) {
                DropTables::Entry entry;entry.petalIndex=item.index;entry.probability=1;
                entry.rates=content.garbageDropRates();
                const auto minimum=content.petal(item.index).minRarity;
                for(auto& rates:entry.rates)rates.erase(std::remove_if(rates.begin(),rates.end(),
                    [&](const DropRate& r){return rarityIndex(r.rarity)<rarityIndex(minimum);}),rates.end());
                garbageTable.push_back(std::move(entry));
            }
        }
        std::vector<std::vector<Entity>> garbagePaid(garbageTable.size());
        const std::vector<DropTables::Entry>& table = visibleGarbage ? garbageTable : mobRarity == Rarity::Common
                                                          ? tables_.forMob(mobIndex)
                                                          : tables_.guaranteedForMob(mobIndex);

        // Marked before a single drop is rolled: a corpse with an empty table
        // must be just as finished as one that paid out.
        world.add<LootAwarded>(corpse);
        if (eligible_.empty()) continue;

        // Resolve rights from surviving eligible contributors, rather than
        // requiring the last hitter or first ledger body to still exist.
        // A stale final hitter must not erase the rewards earned by remaining
        // contributors, including a splitter whose credited half has died.
        const bool hasClaimant = std::any_of(eligible_.begin(), eligible_.end(),
            [&](Entity player) { return world.has<PlayerTag>(player); });
        if (!hasClaimant) continue;
        if (waveBoss) waveReady = nowMillis + (hyperKill ? 720000.0 : 360000.0);

        // A worn magic orb rewrites what this mob leaves behind: everything
        // with a magic form drops as that form instead, and nothing gets past
        // the ORB'S OWN TIER. A common orb turns every leaf this mob would
        // have dropped into a common magic leaf and nothing else -- no
        // ordinary leaf, and no magic leaf above common -- so the way to farm
        // better magic petals is to carry a better orb.
        //
        // Read off EACH eligible flower, and applied to that flower's copy
        // alone: an orb is a bonus its wearer carries, not a rule for the whole
        // kill. So a petal with a magic form splits into one drop per orb tier
        // among the eligible -- the flowers with no orb share the ordinary one
        // -- each reserved for its own group, which replication already hides
        // from everybody else. kRarityCount is "no orb worn", which is almost
        // every flower in the game, and then nothing splits.
        const auto baseEligible=eligible_;
        if (apexKill && !table.empty()) {
            std::vector<const DropTables::Entry*> apexPool;
            for (const auto& entry : table) {
                if (entry.kind == DropTables::Kind::RandomPetal ||
                    (entry.petalIndex < content.petalCount() &&
                     !content.petal(entry.petalIndex).isAdminPetal &&
                     !petalHasOwnSource(content.petal(entry.petalIndex)) &&
                     petalComesAt(content.petal(entry.petalIndex), Rarity::Ascendant))) {
                    apexPool.push_back(&entry);
                }
            }
            std::vector<std::string> paidAccounts;
            for (const Entity claimant : baseEligible) {
                if (apexPool.empty()) break;
                const auto* account = world.tryGet<PlayerAccount>(claimant);
                const auto* location = world.tryGet<Transform>(claimant);
                if (!account || account->connection == 0 || account->userId.empty() ||
                    !location || location->realm != realm ||
                    std::find(paidAccounts.begin(), paidAccounts.end(), account->userId) != paidAccounts.end())
                    continue;
                const bool contributed = std::any_of(ranked_.begin(), ranked_.end(),
                    [&](const Bounty::Share& share) {
                        return (share.owner == account->connection || share.player == claimant) &&
                               share.damage >= damageFloor;
                    });
                if (!contributed) continue;
                paidAccounts.push_back(account->userId);
                if (!rng.chance(kApexPetalDropChance)) continue;
                // One personal roll per kill. Magic conversion only changes the
                // selected type, so it cannot change the odds or rarity.
                const auto* entry = apexPool[rng.below(apexPool.size())];
                auto petal = entry->kind == DropTables::Kind::RandomPetal
                    ? tables_.randomPetal(rng, Rarity::Ascendant) : entry->petalIndex;
                if (petal == kInvalidIndex) continue;
                const auto magic = entry->magicPetal != kNoPetal
                    ? entry->magicPetal : content.magicFormOf(petal);
                const int orbTier = wornOrbTier(world, content, claimant, magicOrb);
                if (magic < content.petalCount() && orbTier != kRarityCount &&
                    orbTier >= rarityIndex(content.petal(magic).minRarity)) petal = magic;
                const Entity drop = spawnDrop(world, petal, Rarity::Ascendant, at, realm, {claimant});
                if (drop != NULL_ENTITY) {
                    auto& item = world.get<DropItem>(drop);
                    item.humansOnly = true;
                    item.singleCopy = true;
                    fresh_.push_back(drop);
                }
            }
        }
        std::vector<std::uint16_t> hyperEternalPool;
        std::vector<HyperEternalDropWeight> hyperWeights;
        if(hyperKill)for(const auto& entry:table)
            if(entry.petalIndex!=kInvalidIndex&&petalComesAt(content.petal(entry.petalIndex),Rarity::Unique)&&
               std::find(hyperEternalPool.begin(),hyperEternalPool.end(),entry.petalIndex)==hyperEternalPool.end())
            {
                hyperEternalPool.push_back(entry.petalIndex);
                hyperWeights.push_back(hyperEternalDropWeight(entry.rates, content.petal(entry.petalIndex).minRarity));
            }
        struct ClaimantRolls {
            int trials;
            int orbTier;
        };
        std::vector<ClaimantRolls> claimantRolls;
        claimantRolls.reserve(baseEligible.size());
        int trials = 0;
        for(const auto claimant:baseEligible) {
            const int bonus=bonusMultiplier
                ? clamp(bonusMultiplier(claimant),1,3) : 1;
            const int orbTier = visibleGarbage ? kRarityCount : wornOrbTier(world,content,claimant,magicOrb);
            int rolls = bonusLootTrialsForRarity(mobRarity, bonus);
            if (hyperKill) {
                const auto* account = world.tryGet<PlayerAccount>(claimant);
                const int tenths = hyperRewardTenths(world.get<Bounty>(corpse), claimant,
                    account ? account->connection : 0, maxHealth);
                if (tenths < 10) {
                    reducedClaimants.push_back(claimant);
                } else if (tenths > 10) {
                    const int scaled = rolls * tenths;
                    rolls = scaled / 10;
                    const int remainder = scaled % 10;
                    if (remainder != 0 && rng.chance(remainder / 10.0)) ++rolls;
                }
            }
            claimantRolls.push_back({rolls,orbTier});
            trials = std::max(trials, rolls);
        }
        const int baseTrials=baseLootTrialsForRarity(mobRarity);
        const auto eternalChances=hyperEternalChances(hyperWeights,kHyperEternalDropBudget/baseTrials);
        const bool grouped=std::any_of(table.begin(),table.end(),[](const auto& e){return e.rollGroup!=0;});
        const bool legacy=std::any_of(table.begin(),table.end(),[](const auto& e){return e.rates.empty();});
        for (int trial=0;trial<trials;++trial) {
            eligible_.clear();eligibleOrbs_.clear();bool anyOrb=false;
            for(std::size_t i=0;i<baseEligible.size();++i) {
                const auto& claimant=claimantRolls[i];
                if(trial>=claimant.trials)continue;
                eligible_.push_back(baseEligible[i]);
                eligibleOrbs_.push_back(claimant.orbTier);
                anyOrb=anyOrb||claimant.orbTier!=kRarityCount;
            }
            // One shared Eternal roll across all rows in this trial.
            std::size_t eternalPick=hyperEternalPool.size();
            if(hyperKill&&!hyperEternalPool.empty()) {
                double roll=rng.unit();
                for(std::size_t i=0;i<eternalChances.size();++i)
                    if((roll-=eternalChances[i])<0){eternalPick=i;break;}
            }
            if(eternalPick<hyperEternalPool.size()){
                const auto petal=hyperEternalPool[eternalPick];
                hyperEternalRoll=true;
                auto magic=visibleGarbage?kInvalidIndex:content.magicFormOf(petal);
                if(!visibleGarbage)for(const auto& entry:table)
                    if(entry.petalIndex==petal && entry.magicPetal!=kNoPetal){magic=entry.magicPetal;break;}
                for(std::size_t i=0;i<eligible_.size();++i){
                    const int tier=eligibleOrbs_[i];
                    const bool convert=magic!=kInvalidIndex&&tier!=kRarityCount&&tier>=rarityIndex(content.petal(magic).minRarity);
                    stackDrop(convert?magic:petal,convert?clampRarity(std::min(tier,rarityIndex(Rarity::Unique))):Rarity::Unique,at,realm,std::vector<Entity>{eligible_[i]});
                }
                hyperEternalRoll=false;
            }
            selected_.clear();
            rollTable(table, mobRarity, rng);
            const double groupRoll=grouped?rng.unit():0;
            double groupOffset=0;
            const bool gradeEveryRow = rarityIndex(mobRarity) <= rarityIndex(Rarity::Uncommon);
            const std::size_t featured = gradeEveryRow || !legacy ? 0 : pickFeatured(rng);
            for (std::size_t row = 0; row < selected_.size(); ++row) {
                const DropTables::Entry* entry = selected_[row];
                if(!entry->rates.empty()){
                    double sample=rng.unit();
                    if(entry->rollGroup){
                        sample=groupRoll-groupOffset;
                        for(const auto& rate:ratesFor(entry->rates,mobRarity))groupOffset+=rate.probability;
                    }
                    const auto ordinary=entry->petalIndex;
                    const auto magic=visibleGarbage?kInvalidIndex:entry->magicPetal!=kNoPetal?entry->magicPetal:content.magicFormOf(ordinary);
                    // Same underlying trial for every claimant. Orb groups
                    // receive their own conditional conversion and cap only.
                    for(std::size_t first=0;first<eligible_.size();++first){
                        const int tier=eligibleOrbs_[first];
                        if(std::find(eligibleOrbs_.begin(),eligibleOrbs_.begin()+static_cast<std::ptrdiff_t>(first),tier)!=
                           eligibleOrbs_.begin()+static_cast<std::ptrdiff_t>(first))continue;
                        const bool convert=magic!=kInvalidIndex&&tier!=kRarityCount&&
                            tier>=rarityIndex(content.petal(magic).minRarity);
                        const auto& rates=convert&&!entry->magicRates.empty()?entry->magicRates:entry->rates;
                        int rolled=rollDropRates(rates,mobRarity,sample);
                        if(rolled<0)continue;
                        const auto petal=convert?magic:ordinary;
                        if(convert)rolled=std::max(rarityIndex(content.petal(petal).minRarity),std::min(rolled,tier));
                        const auto rarity=clampRarity(rolled);
                        if(!petalComesAt(content.petal(petal),rarity))continue;
                        orbGroup_.clear();
                        for(std::size_t j=first;j<eligible_.size();++j)if(eligibleOrbs_[j]==tier)orbGroup_.push_back(eligible_[j]);
                        stackDrop(petal,rarity,at+Vec2{rng.range(-50.0,50.0),rng.range(-50.0,50.0)},realm,orbGroup_);
                        if(visibleGarbage) {
                            auto& paid=garbagePaid[static_cast<std::size_t>(entry-garbageTable.data())];
                            for(const auto claimant:orbGroup_)if(std::find(paid.begin(),paid.end(),claimant)==paid.end())paid.push_back(claimant);
                        }
                    }
                    continue;
                }
                const bool graded = gradeEveryRow || row == featured;
                const Rarity base = graded
                    ? scaleDropRarity(clampRarity(entry->rarityOffset), mobRarity, rng)
                    : chaffDropRarity(mobRarity);
                Rarity rarity = graded ? finishDropRarity(base, mobRarity, rng) : base;
                // Legacy custom rows have no authored CDF. Hyper must use the
                // same Ultra body / 1.5% Super tail as ordinary high-tier rows,
                // rather than making every chaff row a guaranteed Super.
                if (hyperKill) rarity = rng.chance(.015) ? Rarity::Super : Rarity::Ultra;
                // The Random sentinel resolves per COPY, not per row.
                std::uint16_t petalIndex = entry->kind == DropTables::Kind::RandomPetal
                                               ? tables_.randomPetal(rng, rarity)
                                               : entry->petalIndex;
                // A petal that does not come at the tier this copy rolled is
                // not dropped at all -- not lifted to its floor, which would
                // turn a common spider's third eye into a legendary one.
                if (!petalComesAt(content.petal(petalIndex), rarity)) continue;
                const std::uint16_t magic =
                    anyOrb ? content.magicFormOf(petalIndex) : kInvalidIndex;
                const Vec2 scatter{rng.range(-50.0, 50.0), rng.range(-50.0, 50.0)};
                // Unresolved on purpose, as in the reference: the per-tick pass
                // is what pushes a drop out of the geometry it landed in. Noted
                // so the sweep below tests the scattered position, as the
                // reference's same-step pickup does.
                if (magic == kInvalidIndex) {
                    stackDrop(petalIndex, rarity, at + scatter, realm, eligible_);
                    continue;
                }
                for (std::size_t first = 0; first < eligible_.size(); ++first) {
                    const int tier = eligibleOrbs_[first];
                    // One drop per tier, placed when its first member comes up.
                    if (std::find(eligibleOrbs_.begin(),
                                  eligibleOrbs_.begin() + static_cast<std::ptrdiff_t>(first),
                                  tier) != eligibleOrbs_.begin() +
                                               static_cast<std::ptrdiff_t>(first)) {
                        continue;
                    }
                    orbGroup_.clear();
                    for (std::size_t j = first; j < eligible_.size(); ++j) {
                        if (eligibleOrbs_[j] == tier) orbGroup_.push_back(eligible_[j]);
                    }
                    std::uint16_t groupPetal = petalIndex;
                    Rarity groupRarity = rarity;
                    if (tier != kRarityCount) {
                        groupPetal = magic;
                        // The gate. Applied AFTER the ordinary roll rather than
                        // instead of it, so a low orb does not also flatten the
                        // odds -- it caps what those odds can produce.
                        if (rarityIndex(groupRarity) > tier) groupRarity = clampRarity(tier);
                    }
                    stackDrop(groupPetal, groupRarity, at + scatter, realm, orbGroup_);
                }
            }
        }
        // Each visible type is guaranteed once for each entitled account. The
        // ordinary 30/60/90 (high-tier 30/45/60) trials run first. Only a type
        // that missed every trial gets one conditional, normally graded copy.
        for(std::size_t i=0;i<garbageTable.size();++i) {
            std::vector<Entity> missing;
            for(const auto claimant:baseEligible)
                if(std::find(garbagePaid[i].begin(),garbagePaid[i].end(),claimant)==garbagePaid[i].end())missing.push_back(claimant);
            if(missing.empty())continue;
            const auto& entry=garbageTable[i];double total=0;
            for(const auto& rate:ratesFor(entry.rates,mobRarity))total+=rate.probability;
            if(total<=0)continue;
            const int rolled=rollDropRates(entry.rates,mobRarity,rng.unit()*total);
            if(rolled>=0)stackDrop(entry.petalIndex,clampRarity(rolled),at+Vec2{rng.range(-50.0,50.0),rng.range(-50.0,50.0)},realm,missing);
        }
    }
}

void LootSystem::rollTable(const std::vector<DropTables::Entry>& table, Rarity mobRarity,
                           Rng& rng) {
    // Common mobs roll every row independently, so they can never beat the
    // full set a rarer mob hands out -- and a common kill can still leave
    // nothing at all.
    if (mobRarity == Rarity::Common) {
        for (const DropTables::Entry& entry : table) {
            if(!entry.rates.empty()){selected_.push_back(&entry);continue;}
            if (rng.chance(entry.probability)) selected_.push_back(&entry);
        }
        return;
    }

    // Every mob above common leaves one of every drop it has. Nothing is
    // rolled here: above unusual `probability` is spent in pickFeatured on
    // WHICH row is the graded drop, so a low-probability row is not a rare
    // drop any more, it is a reliably WEAK one. The table handed in is the
    // merged one, so two authored lines naming one petal are one drop rather
    // than two.
    for (const DropTables::Entry& entry : table) {
        // Random jackpot rows are not ordinary guaranteed item types. Their
        // authored chance must survive the non-Common merged-table path.
        if (entry.rates.empty() &&
            (mobRarity == Rarity::Apex || entry.kind == DropTables::Kind::RandomPetal) &&
            !rng.chance(entry.probability)) continue;
        selected_.push_back(&entry);
    }
}

std::size_t LootSystem::pickFeatured(Rng& rng) const {
    // The pre-guaranteed-drops weighted draw, unchanged down to the fallback:
    // a table whose probabilities are all zero picks its last row rather than
    // nothing, so a mob with such a table still leaves one graded item.
    if (selected_.empty()) return 0;
    double total = 0.0;
    for (const DropTables::Entry* entry : selected_) total += entry->probability;
    if (total <= 0.0) return selected_.size() - 1;
    double roll = rng.unit() * total;
    for (std::size_t i = 0; i < selected_.size(); ++i) {
        roll -= selected_[i]->probability;
        if (roll <= 0.0) return i;
    }
    return selected_.size() - 1;
}

} // namespace flix
