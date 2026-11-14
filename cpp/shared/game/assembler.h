#pragma once

#include <chrono>

#include "shared/game/config.h"

namespace flix {

inline constexpr double kBioAssemblerRotationMillis = 3600000.0;
inline constexpr double kBioAssemblerCooldownMillis = 600000.0;
inline constexpr std::uint8_t kAssemblerUnavailableRecipe = 255;

inline double assemblerEpochMillis() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int assemblyRotationSlot(const NpcSpec& spec, double epochMillis, int hourOffset = 0) {
    if (spec.hourlyRecipeSlots <= 0) return -1;
    const auto hour = static_cast<std::int64_t>(std::floor(epochMillis / kBioAssemblerRotationMillis)) +
                      hourOffset;
    const auto count = spec.hourlyRecipeSlots;
    return static_cast<int>((hour % count + count) % count);
}

inline std::uint8_t assemblyRecipeAtSlot(const NpcSpec& spec, int slot) {
    if (slot < 0) return kAssemblerUnavailableRecipe;
    for (std::size_t i = 0; i < spec.recipes.size(); ++i) {
        if (spec.recipes[i].rotationSlot == slot) return static_cast<std::uint8_t>(i);
    }
    return kAssemblerUnavailableRecipe;
}

inline std::uint8_t hourlyAssemblyRecipe(const NpcSpec& spec, double epochMillis,
                                         int hourOffset = 0) {
    const auto index = assemblyRecipeAtSlot(spec, assemblyRotationSlot(spec, epochMillis, hourOffset));
    return index < spec.recipes.size() && !spec.recipes[index].previewOnly
        ? index : kAssemblerUnavailableRecipe;
}

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

/// Limit a batch using the same gross and net output bounds as settlement.
inline int assemblerOutputCapacity(std::int64_t held, std::int64_t produced,
                                   std::int64_t consumed) {
    const auto gross = kMaxStackCount / produced;
    const auto net = produced - consumed;
    const auto room = kMaxStackCount - held;
    return static_cast<int>(net > 0 ? std::min(gross, room / net) : gross);
}

} // namespace flix
