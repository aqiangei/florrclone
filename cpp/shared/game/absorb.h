#pragma once
// Absorbing: petals traded in for XP, from the craft window's Absorb side.
//
// One header because both ends price a request -- the client to print the
// "+N XP" it is about to grant, the server to grant it -- and the two must
// arrive at the same number.
//
// The curve is sized for this game's levels. Low-tier duplicates always grant
// at least one XP, including a new player's first Common. Upper tiers remain
// useful to veterans, but are a supplement to hunting: M/U/S/E pay roughly
// 7.5/10.7/9.3/11.8% of a same-tier Ladybug kill before Absorption talents.
// Every account uses the same table; account age and level never devalue stock.

#include <cmath>
#include <cstdint>

#include "shared/game/config.h"
#include "shared/game/rarity.h"
#include "shared/game/skills.h"

namespace flix {

/// The petal florr refuses to absorb (as it refuses square, which we do not
/// have): everyone starts with it, so it is worth nothing.
inline constexpr const char* kUnabsorbablePetalId = "basic";

/// Common through Eternal. Tracked Hyper/admin holdings have no burn quote.
inline constexpr double kAbsorbXpByTier[] = {1, 4, 16, 80, 400, 2400, 16000, 112000, 800000};

/// How many petal stacks one Absorb request may name. More than a grid holds:
/// a type at every ladder tier for every petal there is.
inline constexpr int kMaxAbsorbStacks = 4096;

/// Whole base XP, summed before applying the talent and rounding once.
inline double absorbUnits(Rarity rarity) {
    const int tier = rarityIndex(rarity);
    return tier >= 0 && tier < 9 ? kAbsorbXpByTier[tier] : 0.0;
}

inline double absorbXpPerPetal(Rarity rarity) { return absorbUnits(rarity); }

/// Whether a petal may be absorbed at `rarity`: anything on the ladder except
/// the basic. Contested Hyper petals and universals are tracked holdings;
/// destroying one is not a trade the game offers.
inline bool absorbable(const PetalConfig& petal, Rarity rarity) {
    return rarityIndex(rarity) < rarityIndex(Rarity::Apex) && petal.id != kUnabsorbablePetalId;
}

/// Reference Absorption talent scales absorbed XP independently of Luck.
inline double absorbXpScale(const SkillSet& skills) {
    return skills.effectScale(SkillId::Absorbing);
}

/// What absorbing a whole request grants: `units` (the sum of
/// count x absorbUnits over it) through the talent, floored once. The nudge
/// keeps a total that is whole on paper -- 50 XP at 2.52x is 126 -- from
/// flooring to the integer under it when the scale is a hair short in binary.
inline double absorbXpTotal(double units, const SkillSet& skills) {
    return std::floor(units * absorbXpScale(skills) + 1e-7);
}

} // namespace flix
