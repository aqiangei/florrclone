#pragma once
#include "shared/core/types.h"
#include "shared/game/rarity.h"

namespace flix {
inline bool dominoHasReroll(Rarity rarity) { return ladderRarity(rarity) == Rarity::Apex; }
inline std::uint8_t rollDominoPips(Rng& rng, Rarity rarity) {
    auto roll = [&]() {
        auto value = rng.below(7);
        if (value == 0 && dominoHasReroll(rarity)) value = rng.below(7);
        return value;
    };
    const auto a = roll(); const auto b = roll();
    return static_cast<std::uint8_t>(a * 7 + b);
}
inline double dominoDamageMultiplier(std::uint8_t pips, Rarity rarity) {
    // Ordinary E[a*b]=9. With one zero-only reroll per end E[a*b]=576/49.
    // Normalize to the shared mean damage so H's expected output stays at
    // 1.5x E rather than stacking a hidden damage buff on its stable rolls.
    const double mean = dominoHasReroll(rarity) ? 576.0 / 49.0 : 9.0;
    return (pips / 7) * (pips % 7) / mean;
}
} // namespace flix
