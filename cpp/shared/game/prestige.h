#pragma once
// Prestige: a harder copy of every biome.
//
// Every world map whose maps.json entry says `"prestige": true` is loaded a
// second time as its PRESTIGE twin (WorldMaps::load): the same file in a realm
// of its own, so the two never see or touch each other. A player who has bought the Prestige
// talent can flip the title screen's yellow arrow and join any biome's twin
// instead of the biome itself; every pad inside a twin leads to another twin.
//
// A twin's bands raise the ordinary roll a tier through Ultra. At the top of
// the curve, most natural Ultra rolls become Super, leaving some Ultra in the
// mix. A separate natural-spawn roll can promote a Super to Eternal.
//
// The PVP arena and the maze are generated rather than authored, have no map
// file to copy, and have no prestige version; neither do the instanced dungeon
// copies (a manifest entry with `copies`), whose broods take their tier from
// the mob the party walked in through.

#include <string>

#include "shared/core/types.h"
#include "shared/game/rarity.h"

namespace flix {

/// The prestige twin's map id is the plain map's id behind this prefix:
/// `prestige_garden`. An id rather than a flag on the side because a pad's
/// `targetMap`, an admin's console and every log line name a map by its id,
/// and a twin that answered to `garden` would be the plain garden to all of
/// them. The server's biome name for a twin's realm carries the same prefix,
/// so a squad or a summons announcement never mistakes the prestige garden
/// for the garden.
inline constexpr const char* kPrestigeMapPrefix = "prestige_";

/// What the title screen's arrow and the minimap caption call the place.
inline constexpr const char* kPrestigeLabel = "Prestige";

/// How many tiers a twin's spawn roll adds.
inline constexpr int kPrestigeRarityStep = 1;

/// The highest tier of the ordinary promotion curve.
inline constexpr Rarity kPrestigeRarityCap = Rarity::Super;

/// The arrow's colour on the title screen and the talent's node colour.
inline constexpr std::uint32_t kPrestigeYellow = 0xFFD43Bu;

/// A mob rolled at `rolled` for an ordinary band, as it stands in that band's
/// prestige twin: one tier up, and never past super.
inline constexpr Rarity prestigeRarity(Rarity rolled) {
    const int raised = rarityIndex(rolled) + kPrestigeRarityStep;
    return raised > rarityIndex(kPrestigeRarityCap) ? kPrestigeRarityCap
                                                    : static_cast<Rarity>(raised);
}

inline constexpr double kPrestigeUltraPromotionChance = 0.75;

/// Keep the deepest bands Super-heavy while retaining some quicker Ultra fights.
inline Rarity rollPrestigeAmbientRarity(Rarity rolled, Rng& rng) {
    if (rolled == Rarity::Ultra && !rng.chance(kPrestigeUltraPromotionChance))
        return Rarity::Ultra;
    return prestigeRarity(rolled);
}

/// The base tier for the prestige XP cap, one below the spawned tier.
/// A common has no tier below and uses its own XP as the base.
inline constexpr Rarity prestigeXpRarity(Rarity rarity) {
    const int lowered = rarityIndex(rarity) - kPrestigeRarityStep;
    return lowered < 0 ? Rarity::Common : static_cast<Rarity>(lowered);
}

/// True for tiers barred from regular and manually requested spawns in a twin.
inline constexpr bool barredFromPrestige(Rarity rarity) {
    return rarityIndex(rarity) > rarityIndex(kPrestigeRarityCap);
}

/// `prestige_garden` -> `garden`; anything else unchanged.
inline std::string stripPrestigePrefix(const std::string& id) {
    const std::string prefix = kPrestigeMapPrefix;
    return id.rfind(prefix, 0) == 0 ? id.substr(prefix.size()) : id;
}

} // namespace flix
