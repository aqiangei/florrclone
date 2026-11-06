#pragma once

#include "shared/game/rarity.h"

namespace flix {
// Corpse transport, inspired by Dark Mark; the original long confinement is
// deliberately not imported. H's short hold ends on death or leaving the game.
inline constexpr double kDarkMarkRange = 45.0;
inline constexpr double kDarkMarkReloadMillis = 20000.0;
inline constexpr double kDarkMarkProtectionMillis = 300000.0;
inline constexpr double kHyperMarkHoldMillis = 30000.0;
inline double darkMarkDelayMillis(Rarity rarity) {
    const int tier = rarityIndex(ladderRarity(rarity));
    return tier >= 8 ? 2000.0 : tier == 7 ? 4000.0 : tier == 6 ? 6000.0 : 8000.0;
}
} // namespace flix
