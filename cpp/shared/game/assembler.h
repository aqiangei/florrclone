#pragma once

#include "shared/game/config.h"

namespace flix {

/// Only the Super fragment-and-gear recipe pays the premium. The alternate
/// Mimic + RAM recipe and the equivalent recipes at other tiers keep their price.
inline std::int64_t assemblerInputCount(const AssemblyRecipe& recipe, const ContentRegistry& registry,
                                       Rarity tier, std::uint16_t ingredient, std::int64_t base) {
    if (tier == Rarity::Super && recipe.inputs.size() == 2 && recipe.outputs.size() == 1 &&
        recipe.outputs.front().petal == registry.petalIndex("sawblade") &&
        recipe.takes(registry.petalIndex("fragment")) > 0 &&
        recipe.takes(registry.petalIndex("cogwheel")) > 0) {
        if (ingredient == registry.petalIndex("fragment")) return 28;
        if (ingredient == registry.petalIndex("cogwheel")) return 2;
    }
    return base;
}

} // namespace flix
