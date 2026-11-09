#include "server/replication.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "server/systems/petals.h"
#include "shared/game/constants.h"
#include "shared/game/spatial.h"

namespace flix {

void ClientView::deferPickup(const WireEvent& pickup) {
    for (auto& pending : deferredPickups) {
        if (pending.amount != pickup.amount || pending.flag != pickup.flag) continue;
        const auto remaining = UINT32_MAX - pending.pickupQuantity;
        pending.pickupQuantity += std::min(pickup.pickupQuantity, remaining);
        return;
    }
    deferredPickups.push_back(pickup);
}

void ClientView::reset() {
    tracked.clear();
    deferredPickups.clear();
    selfSent = false;
}

namespace {

/// A position as the client decodes it from ByteWriter::position: each axis
/// through an f32. What Tracked holds, so that a FieldPositionDelta is
/// measured from the very value the client will add it to.
Vec2 wirePosition(Vec2 p) {
    return {static_cast<double>(static_cast<float>(p.x)),
            static_cast<double>(static_cast<float>(p.y))};
}

/// FNV-1a over a skin id, for Tracked's change detection only.
///
/// A collision would cost one player one missed skin change until anything
/// else about their visuals moved, which for server-minted ids (a millisecond
/// clock plus a random tail) is not a case that occurs. The alternative --
/// keeping the id itself beside every tracked entity in every view -- costs
/// memory on every mob on the map, for all of them.
std::uint32_t skinIdHash(const std::string& id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

/// The radius an entity is described at: its body's, or 10 for one without.
///
/// A web is the exception. It has no body -- it is a patch of ground, not a
/// thing -- and gardn draws it scaled to the reach of its field (RenderWeb.cc
/// scales the art by `ent.radius / 10`), so that is what it is sent at. The
/// other fields keep the 10: a pollen puff's disc is the grain lying on the
/// ground, not the reach of its dust.
double replicatedRadius(World& world, Entity e) {
    if (const Body* body = world.tryGet<Body>(e)) return body->radius;
    // A sacrifice's star, which is twice the mob it summons.
    if (const SacrificeRitual* ritual = world.tryGet<SacrificeRitual>(e)) return ritual->radius;
    // An opened tomb, at the size the tomb was drawn.
    if (const MobRemains* remains = world.tryGet<MobRemains>(e)) return remains->radius;
    if (const GroundEffect* effect = world.tryGet<GroundEffect>(e)) {
        if ((effect->kind == GroundEffectKind::Web || effect->kind == GroundEffectKind::ElectricWeb) &&
            effect->radius > 0.0) return effect->radius;
    }
    return 10.0;
}

/// The facing an entity is described at: its transform's, except for a
/// flower's petal, which is sent the way its INSTANCE faces. That is the same
/// number for every petal but a clump's grain, whose transform holds the
/// clump's shared ring bearing while the grain itself faces out along its own
/// sub-bearing -- and the client needs the latter to turn a stinger's grains
/// in towards the middle of their clump.
double replicatedAngle(World& world, Entity e, const Transform& transform) {
    if (const PetalInstance* instance = world.tryGet<PetalInstance>(e)) {
        return instance->facingAngle;
    }
    return transform.angle;
}

/// Whose a record is, from the VIEWER's side: SpawnForeign when a player other
/// than the viewer owns it, plus SpawnIsPet on a shot a pet fired. Zero for
/// anything no player owns and for everything of the viewer's own.
///
/// A petal names its flower and a pet its owner. A shot names both the body
/// that fired it and the player it credits, which differ exactly when a pet
/// fired it -- and it can outlive either, so a shot is judged by its own
/// side first: a wild mob's is never on the players' team.
///
/// The viewer is matched by connection as well as by body, because a
/// splitter's parked half is not `viewer` and is still the viewer's own.
std::uint8_t ownershipFlags(World& world, Entity e, Entity viewer,
                            net::ConnectionId viewerOwner) {
    Entity player = NULL_ENTITY;
    std::uint8_t flags = 0;
    if (const PetalInstance* instance = world.tryGet<PetalInstance>(e)) {
        player = instance->owner;
    } else if (const Pet* pet = world.tryGet<Pet>(e)) {
        player = pet->owner;
    } else if (const Projectile* shot = world.tryGet<Projectile>(e)) {
        const Faction* side = world.tryGet<Faction>(e);
        if (side == nullptr || side->team != Team::Players) return 0;
        player = shot->creditTo;
        if (shot->owner != shot->creditTo) flags |= net::SpawnIsPet;
    } else if (const GroundEffect* field = world.tryGet<GroundEffect>(e)) {
        player = field->owner;
        if (const Pet* pet = world.tryGet<Pet>(player)) {
            player = pet->owner;
            flags |= net::SpawnIsPet;
        } else if (!world.has<PlayerTag>(player)) return 0;
    } else if (const PlantedMeteor* meteor = world.tryGet<PlantedMeteor>(e)) {
        player = meteor->owner;
    } else if (const HoneyBait* honey = world.tryGet<HoneyBait>(e)) {
        player = honey->owner;
    } else {
        return 0;
    }
    if (player == viewer) return flags;
    const PlayerAccount* account = world.tryGet<PlayerAccount>(player);
    if (account != nullptr && viewerOwner != 0 && account->connection == viewerOwner) {
        return flags;
    }
    return flags | net::SpawnForeign;
}

} // namespace

std::uint8_t computeEntityState(World& world, Entity e, double nowMillis) {
    std::uint8_t state = 0;

    if (const Health* health = world.tryGet<Health>(e)) {
        if (nowMillis < health->flashUntilMillis) state |= net::StateHurt;
        if (nowMillis < health->invulnerableUntilMillis) state |= net::StateInvulnerable;
    }
    if (const Afflictions* afflictions = world.tryGet<Afflictions>(e)) {
        if (afflictions->poisoned(nowMillis)) state |= net::StatePoisoned;
        if (afflictions->slowed(nowMillis)) state |= net::StateSlowed;
    }
    if (const PlayerInput* input = world.tryGet<PlayerInput>(e)) {
        if (input->current.attacking()) state |= net::StateAttacking;
        if (input->current.defending()) state |= net::StateDefending;
    }
    if (const MobAi* ai = world.tryGet<MobAi>(e)) {
        // A mending mob holds its target only to run from it, and a starfish
        // spinning as if it were hunting would say the opposite. So does a
        // digger on its last tenth.
        if (ai->target != NULL_ENTITY && !(ai->recoverPerSecond > 0.0)) {
            state |= net::StateChasing;
        }
        if (nowMillis < ai->stingLoadedAtMillis) state |= net::StateUnloaded;
    }
    // On a pad and charging it -- which the pad a jump put the flower down on
    // is not, until the flower has stepped off it. A cooldown blocks the jump,
    // not the charge, so a flower back on that pad spins all the same.
    if (const TeleporterState* pads = world.tryGet<TeleporterState>(e)) {
        if (pads->pad >= 0 && !pads->arriving) state |= net::StatePadCharging;
    }
    if (world.has<Dead>(e)) state |= net::StateDead;
    // A sacrifice whose pentagram is drawn and whose mob is out. See
    // net::StateRitualComplete.
    if (const SacrificeRitual* ritual = world.tryGet<SacrificeRitual>(e)) {
        if (ritual->mob != NULL_ENTITY) state |= net::StateRitualComplete;
    }

    return state;
}

PlayerVisualState computePlayerVisuals(World& world, Entity e, double nowMillis) {
    PlayerVisualState out;
    if (!world.has<PlayerTag>(e)) return out;
    if(const auto* burrow=world.tryGet<BurrowState>(e))if(burrow->active&&nowMillis<burrow->untilMillis)out.renderFlags|=PlayerRenderBurrowed;
    if (const auto* swallowed = world.tryGet<WormSwallow>(e);
        swallowed && swallowed->worm != NULL_ENTITY && nowMillis < swallowed->releaseAtMillis)
        out.renderFlags |= PlayerRenderSwallowed;

    if (const PlayerVisuals* visuals = world.tryGet<PlayerVisuals>(e)) {
        out.faceFlags = visuals->faceFlags;
        out.equipFlags = visuals->equipFlags;
        out.renderFlags |= visuals->renderFlags;
        // Borrowed, not copied; see PlayerVisualState::equippedSkinId.
        if (!visuals->equippedSkinId.empty()) out.equippedSkinId = &visuals->equippedSkinId;
        if (visuals->glitched) out.renderFlags |= PlayerRenderGlitch;
        // Corruption is a FACE, not a skin: the flower that cracked a Flower
        // petal open turns on everyone, and the face is the only warning the
        // players around it get before its ring starts biting them.
        if (visuals->corrupted || visuals->equippedCorruption || visuals->nearbyCorruption ||
            visuals->hyperCorruptionExposed) out.faceFlags |= FaceHasCorruption;
    }
    if (const Afflictions* afflictions = world.tryGet<Afflictions>(e)) {
        if (afflictions->poisoned(nowMillis)) out.faceFlags |= FacePoisoned;
        // A dandelion's lockout, which the client washes the body toward white
        // for. Derived from the timer rather than stored on PlayerVisuals for
        // the reason poison is: the affliction expires on its own clock, and a
        // flag written when it landed would need a second writer to clear it.
        if (afflictions->healBlocked(nowMillis)) out.faceFlags |= FaceDandelioned;
    }
    if (const PlayerInput* input = world.tryGet<PlayerInput>(e)) {
        // The ring itself gives defend precedence when both keys are held, so
        // the face must make the same choice rather than advertising a lunge.
        if (input->current.defending()) out.faceFlags |= FaceDefending;
        else if (input->current.attacking()) out.faceFlags |= FaceAttacking;
    }
    if (world.has<Dead>(e)) out.faceFlags |= FaceDeadEyes;
    // A bandage's Undead, which everyone sees: the flower is wrapped in it.
    if (world.has<Undead>(e)) out.faceFlags |= FaceUndead;
    if (const PlayerProgress* progress = world.tryGet<PlayerProgress>(e)) {
        out.level = static_cast<std::uint16_t>(std::max(1, progress->level));
    }
    // Shell's shield, measured against the pool it is protecting: the bar the
    // client draws it on is the health bar, so max health is the only scale it
    // can be quantised against.
    if (const ShieldState* shield = world.tryGet<ShieldState>(e)) {
        const Health* health = world.tryGet<Health>(e);
        if (shield->active() && health != nullptr && health->max > 0) {
            out.shield = static_cast<std::uint8_t>(
                std::lround(clamp(shield->amount / health->max, 0.0, 1.0) * 255.0));
        }
    }
    if (const ArenaScore* arena = world.tryGet<ArenaScore>(e)) {
        out.arenaScore = static_cast<std::uint32_t>(
            clamp(arena->score, 0.0, static_cast<double>(0xFFFFFFFFu)));
    }
    if (const Loadout* loadout = world.tryGet<Loadout>(e)) {
        // The best rarity ANYWHERE in the loadout, empty slots ignored: the
        // level label under the flower is tinted with it, which is how a
        // passing flower advertises what it is carrying.
        for (const LoadoutSlot& slot : loadout->slots) {
            if (slot.empty()) continue;
            if (petalTier(slot.rarity) > petalTier(out.bestRarity) ||
                (slot.rarity == Rarity::PermanentHyper && out.bestRarity == Rarity::Apex)) {
                out.bestRarity = slot.rarity;
            }
        }
    }
    return out;
}

void Replicator::prepareParallelReads() {
    (void)componentId<Afflictions>();
    (void)componentId<ArenaScore>();
    (void)componentId<Body>();
    (void)componentId<BurrowState>();
    (void)componentId<WormSwallow>();
    (void)componentId<Dead>();
    (void)componentId<DropItem>();
    (void)componentId<Faction>();
    (void)componentId<GhostTag>();
    (void)componentId<GroundEffect>();
    (void)componentId<Health>();
    (void)componentId<Loadout>();
    (void)componentId<ManaPool>();
    (void)componentId<MobAi>();
    (void)componentId<MobRemains>();
    (void)componentId<MobRingPetal>();
    (void)componentId<NetId>();
    (void)componentId<Npc>();
    (void)componentId<Pet>();
    (void)componentId<PetalInstance>();
    (void)componentId<PetalSlotState>();
    (void)componentId<PlayerAccount>();
    (void)componentId<PlayerInput>();
    (void)componentId<PlayerLocation>();
    (void)componentId<PlayerProgress>();
    (void)componentId<PlayerTag>();
    (void)componentId<PlayerVisuals>();
    (void)componentId<Projectile>();
    (void)componentId<Replicated>();
    (void)componentId<SacrificeRitual>();
    (void)componentId<ShieldState>();
    (void)componentId<TeleporterState>();
    (void)componentId<Transform>();
    (void)componentId<Undead>();
}

void Replicator::build(World& world, Entity viewer, ClientView& view,
                       const Frame& frame, ByteWriter& out) {
    const Transform* viewerTransform = world.tryGet<Transform>(viewer);
    if (!viewerTransform) return;

    const Vec2 centre = viewerTransform->position;
    // The viewer sees its own realm and nothing else. Every realm's
    // coordinates start at (0, 0), so a position alone says nothing about
    // whether an entity is anywhere near this client -- it has to be in the
    // same space first.
    const Realm realm = viewerTransform->realm;
    Vec2 viewport{kViewportWidth, kViewportHeight};
    if (frame.viewport) {
        viewport = *frame.viewport;
    } else if (const PlayerLocation* location = world.tryGet<PlayerLocation>(viewer)) {
        viewport = location->viewport;
    }
    // A RECTANGLE, per axis, sized off the window the client says it is
    // drawing: the reference builds the same box every frame from the viewport
    // reported by the latest input packet, so a resize or a zoom widens what is
    // streamed on the very next tick rather than at the next join. The bound is
    // exclusive at exactly the edge, as the reference's `>=` test is.
    //
    // The drawn half, plus a margin. Up to the default screen the margin
    // scales with the viewport, so the box is exactly viewportReach viewports.
    // Past it, the margin stops growing. The margin exists so an entity is
    // known before it comes on screen, and things move at the same speed in
    // world units however far out the camera is. A ten-screen antennae view
    // still has to stream every entity it draws (all of the drawn half), but
    // not a margin ten screens wide: that would be the whole map.
    const auto halfExtent = [&](double axis, double defaultAxis, double multiple) {
        return axis * 0.5 + std::min(axis, defaultAxis) * (multiple - 0.5);
    };
    const double outerReach = frame.loadShedding ? 1.2 : viewportReach;
    const double reachX = halfExtent(viewport.x, kViewportWidth, outerReach);
    const double reachY = halfExtent(viewport.y, kViewportHeight, outerReach);

    const auto outsideView = [&](Vec2 at) {
        const double dx = at.x - centre.x;
        const double dy = at.y - centre.y;
        return (dx < 0 ? -dx : dx) >= reachX || (dy < 0 ? -dy : dy) >= reachY;
    };
    // The inner box, on the same axes: inside it an entity is drawn, or close
    // enough to being drawn that it has to be current. Built the same way, so
    // everything on a big screen is updated every snapshot and the margin
    // around it stays the default screen's.
    // Under load, keep the full visible rectangle but sample distant mobs
    // at the existing far cadence even when the camera is pulled back.
    const double nearWidth = frame.loadShedding ? std::min(viewport.x, kViewportWidth) : viewport.x;
    const double nearHeight = frame.loadShedding ? std::min(viewport.y, kViewportHeight) : viewport.y;
    const double nearX = halfExtent(nearWidth, kViewportWidth, nearReach);
    const double nearY = halfExtent(nearHeight, kViewportHeight, nearReach);
    // Measured to the nearest edge of the BODY, not its centre. The margin is
    // a quarter screen, 270 units top and bottom, and an ultra is a radius of
    // 300 and more: tested on its centre, one can stand half on screen while
    // it is still in the far band, and step there at a quarter rate. The
    // extra quarter covers art drawn past the hitbox (visual_scale reaches
    // ~1.16) and the bar under the body.
    constexpr double kDrawnReachPerRadius = 1.25;
    const auto outsideNearBox = [&](Vec2 at, double radius) {
        const double reach = radius * kDrawnReachPerRadius;
        const double dx = at.x - centre.x;
        const double dy = at.y - centre.y;
        return (dx < 0 ? -dx : dx) - reach >= nearX || (dy < 0 ? -dy : dy) - reach >= nearY;
    };
    // At most three entries, scanned linearly: a set for a squad would cost
    // more to build each tick than it could ever save looking through.
    const auto exempt = [&](Entity e) {
        return frame.alwaysVisible != nullptr &&
               std::find(frame.alwaysVisible->begin(), frame.alwaysVisible->end(), e) !=
                   frame.alwaysVisible->end();
    };

    // --- gather what is in view ------------------------------------------
    candidates_.clear();
    constexpr double kNearbyLootReach = 700.0;
    std::array<Candidate, 48> nearbyLoot{};
    std::size_t nearbyLootCount = 0;
    const auto nearer = [](const Candidate& a, const Candidate& b) {
        if (a.selectionKey != b.selectionKey) return a.selectionKey < b.selectionKey;
        return a.netId < b.netId;
    };
    // The connection behind the viewer's body, which is what a drop's
    // reservation is keyed by -- a body dies, a claim does not. Read once
    // rather than per candidate entity.
    const PlayerAccount* viewerAccount = world.tryGet<PlayerAccount>(viewer);
    const net::ConnectionId viewerOwner =
        viewerAccount != nullptr ? viewerAccount->connection : 0;
    const auto gather = [&](Entity e, const NetId& id, const Transform& transform) {
        // Cull before looking up claim and ghost components. Only in-view
        // drops can consume a viewer's snapshot budget.
        if (e != viewer && transform.realm != realm) return;
        if (e != viewer && outsideView(transform.position) && !exempt(e)) return;
        if (world.has<GhostTag>(e)) {
            const auto* visuals = world.tryGet<PlayerVisuals>(viewer);
            if (!visuals || !visuals->seesGhosts) return;
        }
        const DropItem* drop = world.tryGet<DropItem>(e);
        if (drop) {
            if (!drop->eligible.empty() && !claimed(drop->eligible, viewer, viewerOwner)) return;
            if (claimed(drop->pickedUpBy, viewer, viewerOwner)) return;
        }
        // The viewer's own body is always replicated, however the camera sits:
        // losing it would leave the client with no flower of its own to draw,
        // and no SpawnIsSelf record to learn which net id is its own.
        // A squadmate is exempt for its own reason -- see Frame::alwaysVisible
        // -- but only within the viewer's realm: a party member in the maze has
        // no position that means anything on an overworld screen.
        const double distanceSq = flix::distanceSq(transform.position, centre);
        if (drop && world.get<Replicated>(e).rarity < Rarity::Ultra) {
            if (distanceSq > kNearbyLootReach * kNearbyLootReach) return;
            // Keep only the nearest low-tier stacks. High tiers and the
            // observer keep their existing priority in the snapshot.
            const Candidate candidate{e, id.value, distanceSq};
            auto end = nearbyLoot.begin() + nearbyLootCount;
            if (nearbyLootCount < nearbyLoot.size()) {
                *end = candidate;
                ++nearbyLootCount;
                std::push_heap(nearbyLoot.begin(), end + 1, nearer);
            } else if (nearer(candidate, nearbyLoot.front())) {
                std::pop_heap(nearbyLoot.begin(), end, nearer);
                *(end - 1) = candidate;
                std::push_heap(nearbyLoot.begin(), end, nearer);
            }
            return;
        }
        // The observer and nearby claimable loot must survive a full
        // snapshot even if a barrage fills the remaining entity budget.
        const double selectionKey = e == viewer ? -2.0 :
            (drop && !outsideNearBox(transform.position, 0.0) ? -1.0 / (1.0 + distanceSq)
                                                             : distanceSq);
        candidates_.push_back({e, id.value, selectionKey});
    };

    const Vec2 corner{reachX, reachY};
    const Vec2 min = centre - corner;
    const Vec2 max = centre + corner;
    if (frame.entityGrid &&
        std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(max.x) && std::isfinite(max.y)) {
        frame.entityGrid->queryPointBuckets(realm, min, max, nearbyEntities_);
        for (Entity e : nearbyEntities_) gather(e, world.get<NetId>(e), world.get<Transform>(e));

        // A watched body and same-realm squadmates can be visible beyond the
        // indexed rectangle. Add them once if they did not come from a bucket.
        const auto gatherExempt = [&](Entity e) {
            if (!world.has<Replicated>(e) ||
                std::find(nearbyEntities_.begin(), nearbyEntities_.end(), e) != nearbyEntities_.end()) return;
            const auto* id = world.tryGet<NetId>(e);
            const auto* at = world.tryGet<Transform>(e);
            if (id && at) gather(e, *id, *at);
        };
        gatherExempt(viewer);
        if (frame.alwaysVisible) {
            const auto& always = *frame.alwaysVisible;
            for (auto it = always.begin(); it != always.end(); ++it)
                if (*it != viewer && std::find(always.begin(), it, *it) == it) gatherExempt(*it);
        }
    } else {
        Query<NetId, Replicated, Transform>{world}.each(
            [&](Entity e, NetId& id, Replicated&, Transform& at) { gather(e, id, at); });
    }

    candidates_.insert(candidates_.end(), nearbyLoot.begin(), nearbyLoot.begin() + nearbyLootCount);
    if (candidates_.size() > maxEntities) {
        // After the observer and visible loot, nearest wins.
        std::nth_element(candidates_.begin(),
                         candidates_.begin() + static_cast<std::ptrdiff_t>(maxEntities),
                         candidates_.end(),
                         nearer);
        candidates_.resize(maxEntities);
    }
    // In net id order, so each record can name its entity by the gap from the
    // one before (see the Snapshots layout in protocol.h).
    std::sort(candidates_.begin(), candidates_.end(),
              [](const Candidate& a, const Candidate& b) { return a.netId < b.netId; });

    for (auto& tracked : view.tracked) tracked.second.seenThisTick = false;

    // --- header -----------------------------------------------------------
    out.u8(static_cast<std::uint8_t>(net::ServerMessage::Snapshot));
    out.u32(frame.tick);
    out.f64(frame.wallMillis.value_or(frame.nowMillis));

    // The viewer's own position, always: the camera eases toward it every
    // frame, so it never goes through the change-mask path.
    out.position(viewerTransform->position);

    // The viewer's own figures, at the exact values the HUD prints, but only
    // the groups that moved since this viewer was last told (net::SelfFields).
    // They sit still far more than they change, and restating all of them was
    // 34 bytes of every snapshot. Compared at the width they go out in, as
    // a health fraction is, so a change the wire cannot carry is no change.
    ClientView::SelfSent self;
    if (const Health* health = world.tryGet<Health>(viewer)) {
        self.health = static_cast<float>(health->current);
        self.maxHealth = static_cast<float>(health->max);
    }
    // A zero max is a flower wearing nothing that grants a pool, which is
    // almost all of them, and is what tells the client to draw no mana bar.
    if (const ManaPool* mana = world.tryGet<ManaPool>(viewer)) {
        self.mana = static_cast<float>(mana->current);
        self.maxMana = static_cast<float>(mana->max);
    }
    if (const ManaPool* mana = world.tryGet<ManaPool>(frame.splitOther); mana && mana->max > 0.0) {
        if (const NetId* id = world.tryGet<NetId>(frame.splitOther)) {
            self.splitNetId = id->value;
            self.splitMana = static_cast<float>(mana->current);
            self.splitMaxMana = static_cast<float>(mana->max);
        }
    }
    self.level = 1;
    self.wave = frame.wave;
    const auto remainingMillis = [&](double until) {
        return static_cast<float>(std::max(0.0, until - frame.nowMillis));
    };
    if (const auto* secondChance = world.tryGet<SecondChance>(viewer)) {
        self.secondChanceMillis = remainingMillis(secondChance->activeUntilMillis);
        self.secondChanceCooldownMillis = remainingMillis(secondChance->readyAtMillis);
    }
    if (const auto* bandage = world.tryGet<Undead>(viewer))
        self.bandageMillis = remainingMillis(bandage->untilMillis);
    if (const PlayerProgress* progress = world.tryGet<PlayerProgress>(viewer)) {
        self.totalXp = progress->totalXp;
        self.level = static_cast<std::uint16_t>(progress->level);
        self.stars = std::max(0.0, progress->stars);
    }
    const ClientView::SelfSent& last = view.self;
    std::uint8_t selfMask = 0;
    if (!view.selfSent || self.health != last.health || self.maxHealth != last.maxHealth) {
        selfMask |= net::SelfHealth;
    }
    if (!view.selfSent || self.mana != last.mana || self.maxMana != last.maxMana) {
        selfMask |= net::SelfMana;
    }
    if (!view.selfSent || self.totalXp != last.totalXp || self.level != last.level ||
        self.stars != last.stars) {
        selfMask |= net::SelfProgress;
    }
    if (self.splitNetId != last.splitNetId || self.splitMana != last.splitMana ||
        self.splitMaxMana != last.splitMaxMana) {
        selfMask |= net::SelfSplitMana;
    }
    if (self.secondChanceMillis != last.secondChanceMillis ||
        self.secondChanceCooldownMillis != last.secondChanceCooldownMillis ||
        self.bandageMillis != last.bandageMillis) selfMask |= net::SelfCombatTimers;
    if (self.wave != last.wave) selfMask |= net::SelfWave;
    out.u8(selfMask);
    if (selfMask & net::SelfHealth) {
        out.f32(self.health);
        out.f32(self.maxHealth);
    }
    if (selfMask & net::SelfMana) {
        out.f32(self.mana);
        out.f32(self.maxMana);
    }
    if (selfMask & net::SelfProgress) {
        out.f64(self.totalXp);
        out.u16(self.level);
        out.f64(self.stars);
    }
    if (selfMask & net::SelfSplitMana) {
        out.u32(self.splitNetId);
        out.f32(self.splitMana);
        out.f32(self.splitMaxMana);
    }
    if (selfMask & net::SelfCombatTimers) {
        out.f32(self.secondChanceMillis);
        out.f32(self.secondChanceCooldownMillis);
        out.f32(self.bandageMillis);
    }
    if (selfMask & net::SelfWave) {
        out.u32(self.wave.round);
        out.u16(self.wave.remainingMillis);
        out.u16(self.wave.durationMillis);
        out.u8(static_cast<std::uint8_t>(self.wave.theme));
    }
    view.self = self;
    view.selfSent = true;

    // Which of the viewer's own slots are reloading or hurt, and by how much.
    // Only the owner's own bar draws the wedge and the drained tile, so this
    // rides the self block rather than the player's replicated visuals.
    //
    // Streamed rather than evented on purpose: a break event that a client
    // missed would leave its wedge sweeping forever, or its tile drained on a
    // petal long since back at full health.
    static_assert(net::kMaxReportedSlots == kLoadoutActiveSlots,
                  "the wire caps the slot list at the number of orbiting slots");
    const Loadout* viewerLoadout = world.tryGet<Loadout>(viewer);
    const PetalSlotState* viewerSlots = world.tryGet<PetalSlotState>(viewer);

    struct SlotReport {
        double reloadRemaining = 0;
        double health = 1.0;
        /// The whole number the bar prints inside the slot's top border, or
        /// net::kNoSlotCounter for a petal that has none. Rounded UP, so a
        /// sponge still holding a sliver of a hit reads as 1 rather than
        /// dropping to 0 a moment early.
        std::uint16_t counter = net::kNoSlotCounter;
    };
    const auto report = [&](int i, SlotReport& out) {
        if (viewerLoadout == nullptr) return false;
        const auto index = static_cast<std::size_t>(i);
        const LoadoutSlot& slot = viewerLoadout->slots[index];
        if (slot.empty()) return false;
        const PetalSlotState::Slot* live =
            viewerSlots != nullptr ? &viewerSlots->slots[index] : nullptr;

        // A clump -- sand, light, dahlia -- loses and reloads its grains one at
        // a time, and `slot.broken` only goes up once the LAST one is gone. The
        // bar reports the missing grain instead: the wedge runs while any
        // instance is out.
        //
        // Sized by the instance that returns LAST, not the first: taking the
        // soonest would complete the sweep while grains are still missing and
        // then snap it backwards to the next one's remainder.
        if (live != nullptr && live->independent) {
            for (const double readyAt : live->instanceReadyAtMillis) {
                out.reloadRemaining = std::max(out.reloadRemaining, readyAt - frame.nowMillis);
            }
        } else if (slot.broken) {
            out.reloadRemaining = slot.reloadReadyAtMillis - frame.nowMillis;
        }
        out.reloadRemaining = std::max(0.0, out.reloadRemaining);
        if (live != nullptr) out.health = live->healthFraction;
        if (live != nullptr && live->counter >= 0.0) {
            out.counter = static_cast<std::uint16_t>(
                std::clamp(std::ceil(live->counter), 0.0,
                           static_cast<double>(net::kNoSlotCounter - 1)));
        }

        // A slot with a counter is reported every snapshot, even sitting at
        // zero with a whole petal: the number is a gauge, and a gauge that
        // vanishes while it reads empty is one the player learns to distrust.
        return out.reloadRemaining > 0.0 || out.health < 1.0 ||
               out.counter != net::kNoSlotCounter;
    };
    // Counted before it is written: the count leads the list, and ten slots is
    // cheaper to walk twice than a patch-back is to add to the writer.
    std::uint8_t reportCount = 0;
    for (int i = 0; i < kLoadoutActiveSlots; ++i) {
        SlotReport slotReport;
        if (report(i, slotReport)) ++reportCount;
    }
    out.u8(reportCount);
    for (int i = 0; i < kLoadoutActiveSlots; ++i) {
        SlotReport slotReport;
        if (!report(i, slotReport)) continue;
        out.u8(static_cast<std::uint8_t>(i));
        // A reload past a minute is one the bar cannot show meaningfully
        // anyway; clamping keeps the field two bytes.
        out.u16(static_cast<std::uint16_t>(std::min(slotReport.reloadRemaining, 65535.0)));
        out.unitByte(slotReport.health);
        out.u16(slotReport.counter);
    }

    // --- spawns and updates ----------------------------------------------
    const std::size_t spawnCountAt = out.reserveU16();
    std::uint16_t spawnCount = 0;
    const std::size_t updateCountAt = out.reserveU16();
    std::uint16_t updateCount = 0;

    // Two passes over the same candidate list rather than two gathers: the
    // client must create an entity before it can be updated, and interleaving
    // the two record types would force it to tolerate either order.
    std::uint32_t previousSpawnId = 0;
    for (const Candidate& candidate : candidates_) {
        auto it = view.tracked.find(candidate.netId);
        if (it != view.tracked.end()) {
            it->second.seenThisTick = true;
            continue;
        }

        const Replicated& info = world.get<Replicated>(candidate.entity);
        const Transform& transform = world.get<Transform>(candidate.entity);
        const double radius = replicatedRadius(world, candidate.entity);

        std::uint8_t flags = info.spawnFlags;
        if (candidate.entity == viewer) flags |= net::SpawnIsSelf;
        flags |= ownershipFlags(world, candidate.entity, viewer, viewerOwner);
        // Decided here rather than beside the field itself: `flags` is written
        // near the top of the record and the health fraction near the bottom,
        // and the flag is what tells the reader how many bytes the latter is.
        const Health* preHealth = world.tryGet<Health>(candidate.entity);
        const bool spawnWide = net::healthFieldIsWide(preHealth ? preHealth->max : 1.0);
        if (spawnWide) flags |= net::SpawnHealthWide;
        const PlayerAccount* account = world.tryGet<PlayerAccount>(candidate.entity);
        if (account && !account->username.empty()) flags |= net::SpawnHasName;

        out.varU32(candidate.netId - previousSpawnId);
        previousSpawnId = candidate.netId;
        out.u8(static_cast<std::uint8_t>(info.kind));
        out.u16(info.typeIndex);
        out.u8(static_cast<std::uint8_t>(info.rarity));
        out.u8(flags);
        const double angle = replicatedAngle(world, candidate.entity, transform);
        out.position(transform.position);
        out.angle(angle);
        out.f32(static_cast<float>(radius));
        // Current health and state travel WITH the spawn. Without them an
        // entity that enters view already hurt draws a full health bar until
        // it next changes, which for a fleeing mob may be never.
        const Health* spawnHealth = world.tryGet<Health>(candidate.entity);
        const double spawnFraction = spawnHealth ? spawnHealth->fraction() : 1.0;
        const double spawnSent = net::writeHealthFraction(out, spawnFraction, spawnWide);
        out.u8(computeEntityState(world, candidate.entity, frame.nowMillis));
        if (info.kind == net::EntityKind::Drop)
            out.u32(world.get<DropItem>(candidate.entity).quantity);
        const PlayerVisualState visuals =
            computePlayerVisuals(world, candidate.entity, frame.nowMillis);
        if (info.kind == net::EntityKind::Player) {
            out.u8(visuals.faceFlags);
            out.u16(visuals.equipFlags);
            out.u32(visuals.renderFlags);
            out.u16(visuals.level);
            out.u8(static_cast<std::uint8_t>(visuals.bestRarity));
            out.u32(visuals.arenaScore);
            out.u8(visuals.shield);
            out.str(skinIdOf(visuals));
        }
        const PetalInstance* petalInstance = nullptr;
        if (info.kind == net::EntityKind::Petal) {
            // Petals are placed on an absolute ring around the owner's SERVER
            // position every tick, while the owner is drawn at its own eased
            // or predicted one. Without the owner's id the client cannot put
            // the two back together and the ring visibly trails the flower.
            // A flower's petal names its flower; a mob's ring seed names its
            // mob. Both anchor the same way on the client, which is the whole
            // reason a seed is replicated as a Petal at all.
            Entity owner = NULL_ENTITY;
            if (const PetalInstance* instance = world.tryGet<PetalInstance>(candidate.entity)) {
                owner = instance->owner;
            } else if (const MobRingPetal* seed = world.tryGet<MobRingPetal>(candidate.entity)) {
                owner = seed->mob;
            }
            std::uint32_t ownerNetId = 0;
            if (const NetId* ownerId = world.tryGet<NetId>(owner)) ownerNetId = ownerId->value;
            out.u32(ownerNetId);
            petalInstance = world.tryGet<PetalInstance>(candidate.entity);
            out.u8(petalInstance ? (petalInstance->diceFace != 0 ? petalInstance->diceFace : petalInstance->dominoPips)
                            : 255); // Domino pips or the current Dice face.
        }
        if (info.kind == net::EntityKind::Npc) {
            // Which of its mob's recipes an assembler makes; 0 for any other.
            const Npc* npc = world.tryGet<Npc>(candidate.entity);
            out.u8(npc != nullptr ? npc->recipe : 0);
            out.u8(npc != nullptr ? npc->nextRecipe : 0);
            out.f64(npc != nullptr ? npc->recipeRefreshAtMillis : 0);
        }
        if (flags & net::SpawnHasName) out.str(account->username);
        ++spawnCount;

        // Seed the tracked values from what the spawn record just carried, so
        // this tick's update pass has nothing left to say about it.
        ClientView::Tracked tracked;
        tracked.position = wirePosition(transform.position);
        tracked.angle = angle;
        tracked.radius = radius;
        if (const auto* drop = world.tryGet<DropItem>(candidate.entity)) tracked.dropQuantity = drop->quantity;
        tracked.seenThisTick = true;
        tracked.healthFraction = spawnSent;
        tracked.state = computeEntityState(world, candidate.entity, frame.nowMillis);
        tracked.faceFlags = visuals.faceFlags;
        tracked.equipFlags = visuals.equipFlags;
        tracked.renderFlags = visuals.renderFlags;
        tracked.level = visuals.level;
        tracked.bestRarity = static_cast<std::uint8_t>(visuals.bestRarity);
        tracked.arenaScore = visuals.arenaScore;
        tracked.shield = visuals.shield;
        if (petalInstance && (petalInstance->diceFace != 0 || content().petal(info.typeIndex).id == "dice")) {
            tracked.rollingDice = true;
            tracked.petalVariant = petalInstance->diceFace;
        }
        tracked.skinIdHash = skinIdHash(skinIdOf(visuals));
        view.tracked.emplace(candidate.netId, tracked);
    }
    out.patchU16(spawnCountAt, spawnCount);

    const int farStride = frame.loadShedding ? 6 : std::max(1, farSnapshotStride);
    std::uint32_t previousUpdateId = 0;
    for (const Candidate& candidate : candidates_) {
        auto it = view.tracked.find(candidate.netId);
        if (it == view.tracked.end()) continue;
        ClientView::Tracked& tracked = it->second;

        const Transform& transform = world.get<Transform>(candidate.entity);
        // Off-screen and not due this snapshot: say nothing about it at all.
        //
        // Skipping the whole record rather than trimming its fields is the
        // point -- the id gap and the mask are bytes spent before a single
        // field is named, at a hundred-odd entities a snapshot. `tracked` is
        // left exactly as it was, so whenever this entity IS described the
        // diff is still measured against what the client actually holds, and
        // the spawn pass has already marked it seen, so skipping it never
        // reads as a removal.
        //
        // The viewer is exempt for the obvious reason, and a squadmate because
        // it is streamed from any distance precisely so the HUD can point at
        // it (see Frame::alwaysVisible). The radius is the one the client
        // holds, which is the size it draws the body at.
        if (candidate.entity != viewer && !exempt(candidate.entity) &&
            outsideNearBox(transform.position, tracked.radius) &&
            (frame.snapshotIndex + candidate.netId) % static_cast<std::uint32_t>(farStride) != 0) {
            continue;
        }
        const Health* health = world.tryGet<Health>(candidate.entity);
        const Body* body = world.tryGet<Body>(candidate.entity);
        const std::uint8_t state = computeEntityState(world, candidate.entity, frame.nowMillis);
        const PlayerVisualState visuals =
            computePlayerVisuals(world, candidate.entity, frame.nowMillis);

        std::uint16_t mask = 0;
        const auto* dice = tracked.rollingDice ? world.tryGet<PetalInstance>(candidate.entity) : nullptr;
        if (dice && dice->diceFace != tracked.petalVariant) mask |= net::FieldPetalVariant;
        const auto* stack = world.tryGet<DropItem>(candidate.entity);
        if (stack && stack->quantity != tracked.dropQuantity) mask |= net::FieldDropQuantity;
        if (distanceSq(transform.position, tracked.position) >
            tolerances.position * tolerances.position) {
            mask |= net::FieldPosition;
        }
        const double angle = replicatedAngle(world, candidate.entity, transform);
        if (std::fabs(wrapAngle(angle - tracked.angle)) > tolerances.angle) {
            mask |= net::FieldAngle;
        }
        // Compared AT THE WIDTH it would be sent in, which is the only
        // comparison that means anything: judged against the unrounded double,
        // a pool healing by a hundredth of a step would put bytes on the wire
        // every snapshot that decode to the value the client already holds.
        // Judged against a fixed threshold -- as a 1/255 tolerance once was --
        // a petal taking a millionth of an apex mob reads as no change at all,
        // and the bar sits still through the whole fight.
        const bool healthWide = health != nullptr && net::healthFieldIsWide(health->max);
        const double healthFraction =
            health ? net::quantizeHealthFraction(health->fraction(), healthWide) : -1.0;
        if (health && healthFraction != tracked.healthFraction) {
            mask |= net::FieldHealth;
            if (healthWide) mask |= net::FieldHealthWide;
        }
        if (state != tracked.state) mask |= net::FieldState;
        if (body && std::fabs(body->radius - tracked.radius) > tolerances.radius) {
            mask |= net::FieldSize;
        }
        if (world.get<Replicated>(candidate.entity).kind == net::EntityKind::Player &&
            (visuals.faceFlags != tracked.faceFlags ||
             visuals.equipFlags != tracked.equipFlags ||
             visuals.renderFlags != tracked.renderFlags ||
             visuals.level != tracked.level ||
             static_cast<std::uint8_t>(visuals.bestRarity) != tracked.bestRarity ||
             visuals.arenaScore != tracked.arenaScore || visuals.shield != tracked.shield ||
             skinIdHash(skinIdOf(visuals)) != tracked.skinIdHash)) {
            mask |= net::FieldPlayerVisuals;
        }
        // The move as FieldPositionDelta would carry it, rounded to its step.
        // `tracked` advances by exactly what the client will add, so the two
        // stay in step however many deltas are chained.
        const double deltaX =
            std::round((transform.position.x - tracked.position.x) * net::kPositionDeltaScale);
        const double deltaY =
            std::round((transform.position.y - tracked.position.y) * net::kPositionDeltaScale);
        if ((mask & net::FieldPosition) && std::fabs(deltaX) <= 32767.0 &&
            std::fabs(deltaY) <= 32767.0) {
            mask |= net::FieldPositionDelta;
        }
        if (mask == 0) continue;

        out.varU32(candidate.netId - previousUpdateId);
        previousUpdateId = candidate.netId;
        out.u16(mask);
        if (mask & net::FieldPositionDelta) {
            out.i16(static_cast<std::int16_t>(deltaX));
            out.i16(static_cast<std::int16_t>(deltaY));
            tracked.position.x += deltaX / net::kPositionDeltaScale;
            tracked.position.y += deltaY / net::kPositionDeltaScale;
        } else if (mask & net::FieldPosition) {
            out.position(transform.position);
            tracked.position = wirePosition(transform.position);
        }
        if (mask & net::FieldDropQuantity) {
            out.u32(stack->quantity);
            tracked.dropQuantity = stack->quantity;
        }
        if (mask & net::FieldAngle) {
            out.angle(angle);
            tracked.angle = angle;
        }
        if (mask & net::FieldHealth) {
            net::writeHealthFraction(out, health->fraction(), healthWide);
            tracked.healthFraction = healthFraction;
        }
        if (mask & net::FieldState) {
            out.u8(state);
            tracked.state = state;
        }
        if (mask & net::FieldSize) {
            out.f32(static_cast<float>(body->radius));
            tracked.radius = body->radius;
        }
        if (mask & net::FieldPlayerVisuals) {
            out.u8(visuals.faceFlags);
            out.u16(visuals.equipFlags);
            out.u32(visuals.renderFlags);
            out.u16(visuals.level);
            out.u8(static_cast<std::uint8_t>(visuals.bestRarity));
            out.u32(visuals.arenaScore);
            out.u8(visuals.shield);
            out.str(skinIdOf(visuals));
            tracked.faceFlags = visuals.faceFlags;
            tracked.equipFlags = visuals.equipFlags;
            tracked.renderFlags = visuals.renderFlags;
            tracked.level = visuals.level;
            tracked.bestRarity = static_cast<std::uint8_t>(visuals.bestRarity);
            tracked.arenaScore = visuals.arenaScore;
            tracked.shield = visuals.shield;
            tracked.skinIdHash = skinIdHash(skinIdOf(visuals));
        }
        if (mask & net::FieldPetalVariant) {
            out.u8(dice->diceFace);
            tracked.petalVariant = dice->diceFace;
        }
        ++updateCount;
    }
    out.patchU16(updateCountAt, updateCount);

    // --- removals ---------------------------------------------------------
    //
    // Anything tracked that this tick did not see has left view or died. The
    // erase happens only after the removal is written, and the transport is
    // ordered and reliable, so the client cannot be left believing in an
    // entity the server has forgotten.
    removals_.clear();
    for (const auto& entry : view.tracked) {
        if (!entry.second.seenThisTick) removals_.push_back(entry.first);
    }
    std::sort(removals_.begin(), removals_.end());
    out.u16(static_cast<std::uint16_t>(std::min<std::size_t>(removals_.size(), 0xFFFF)));
    std::uint32_t previousRemovalId = 0;
    for (const std::uint32_t netId : removals_) {
        out.varU32(netId - previousRemovalId);
        previousRemovalId = netId;
        view.tracked.erase(netId);
    }

    // --- events -----------------------------------------------------------
    const std::size_t eventCountAt = out.reserveU16();
    std::uint16_t eventCount = 0;
    const auto writeEvent = [&](const WireEvent& event, std::uint32_t other, std::uint8_t flag,
                                bool ownPickup) {
        out.u8(static_cast<std::uint8_t>(event.kind));
        out.u32(event.netId);
        out.u32(other);
        out.f32(static_cast<float>(event.amount));
        out.position(event.position);
        out.f32(static_cast<float>(event.radius));
        out.u8(flag);
        if (event.kind == net::EventKind::PickedUp) { out.u32(event.pickupQuantity); out.boolean(ownPickup); }
        if (event.kind == net::EventKind::Lightning) {
            const std::size_t count = std::min(event.points.size(), net::kMaxLightningTargets);
            out.u8(static_cast<std::uint8_t>(count));
            for (std::size_t i = 0; i < count; ++i) out.position(event.points[i]);
        }
        ++eventCount;
    };
    if (!view.deferredPickups.empty() && view.deferredPickups.front().dealtByConnection == viewerOwner) {
        for (const auto& pickup : view.deferredPickups)
            writeEvent(pickup, pickup.otherNetId, pickup.flag, true);
        view.deferredPickups.clear();
    }
    if (frame.events) {
        // Which hits are this viewer's own. By connection as well as by body
        // -- the viewerOwner the gather above read -- because a splitter's two
        // halves are one person, and the parked half's ring is still theirs.
        // A bot's connection is 0, and so matches nobody.
        const auto dealtByViewer = [&](const WireEvent& event) {
            if (event.kind != net::EventKind::Damage || event.dealtBy == NULL_ENTITY) return false;
            return event.dealtBy == viewer ||
                   (event.dealtByConnection != 0 && event.dealtByConnection == viewerOwner);
        };
        // Events are cosmetic and one-shot. Scoping them to the same reach as
        // entities keeps a busy fight on the far side of the map from costing
        // every client bytes for numbers they will never see. A hit on a
        // squadmate goes out wherever it lands -- the party HUD's avatar
        // flashes with it -- under the same exemption, and in the same realm
        // only, that streams the squadmate itself; marked, so it puts no
        // number in a world nobody is looking at.
        const auto squadmateHit = [&](const WireEvent& event) {
            return event.kind == net::EventKind::Damage && event.struck != NULL_ENTITY &&
                   exempt(event.struck);
        };
        // A hit on one of this viewer's own worn petals flashes its tile on the
        // bar, which is the HUD and not the world: it goes out wherever the
        // petal is -- a wax left behind off screen, or in another realm -- and
        // the blow that broke it goes to the wearer alone.
        const auto ownTile = [&](const WireEvent& event) {
            return event.kind == net::EventKind::Damage && event.wornBy != NULL_ENTITY &&
                   event.wornBy == viewer;
        };
        for (const WireEvent& event : frame.events->events()) {
            if (eventCount == UINT16_MAX) break;
            const bool tile = ownTile(event);
            const bool ownPickup = event.kind == net::EventKind::PickedUp &&
                ((viewerOwner != 0 && event.dealtByConnection == viewerOwner) ||
                 (world.tryGet<NetId>(viewer) && event.otherNetId == world.get<NetId>(viewer).value));
            if (event.wearerOnly && !tile) continue;
            const bool offView = event.positional && outsideView(event.position);
            if (event.positional && !tile && !ownPickup &&
                (event.realm != realm || (offView && !squadmateHit(event)))) {
                continue;
            }
            std::uint8_t flag = event.flag;
            if (event.kind == net::EventKind::Lightning && event.visualSource != NULL_ENTITY) {
                auto owned = ownershipFlags(world, event.visualSource, viewer, viewerOwner);
                if (world.has<PlayerTag>(event.visualSource)) {
                    const auto* account = world.tryGet<PlayerAccount>(event.visualSource);
                    if (event.visualSource != viewer && !(account && viewerOwner != 0 && account->connection == viewerOwner))
                        owned |= net::SpawnForeign;
                }
                if (owned & net::SpawnForeign) flag |= net::LightningForeign;
                if (owned & net::SpawnIsPet) flag |= net::LightningFromPet;
            }
            if (dealtByViewer(event)) flag |= net::DamageByViewer;
            if (offView && event.kind == net::EventKind::Damage) flag |= net::DamageOffView;
            writeEvent(event, tile ? event.wornSlot + 1u : event.otherNetId, flag, ownPickup);
        }
    }
    out.patchU16(eventCountAt, eventCount);
}

} // namespace flix
