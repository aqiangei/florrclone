#pragma once

#include "shared/game/rarity.h"

namespace flix {

// Original Monstera ladder, using the game's softened high-tier healing curve.
inline double monsteraHealPerSecond(Rarity rarity) {
    return 4.0 * petalHealScale(rarity);
}

inline double monsteraHealRadius(Rarity rarity) {
    return 140.0 + 35.0 * ladderIndex(rarity);
}

} // namespace flix
