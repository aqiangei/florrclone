#pragma once

#include "shared/game/config.h"
#include "shared/game/hyper_petal_techniques.h"

namespace flix {

struct HyperPetalTimers {
    double wardReadyAtMillis = 0.0;
    double monsteraReadyAtMillis = 0.0;
    double monsteraAuraReadyAtMillis = 0.0;
    double bloodNeedleReadyAtMillis = 0.0;
    double coinPickupUntilMillis = 0.0;
    double leafRegrowthReadyAtMillis = 0.0;
};

inline bool hyperDefending(const World& world, Entity entity) {
    const auto* input = world.tryGet<PlayerInput>(entity);
    return input && input->current.defending() && !input->current.attacking();
}

inline bool hyperAttacking(const World& world, Entity entity) {
    const auto* input = world.tryGet<PlayerInput>(entity);
    return input && input->current.attacking() && !input->current.defending();
}

inline const HyperTechniqueInfo* wornHyperTechnique(const World& world, const ContentRegistry& registry,
                                                    Entity owner, std::string_view id) {
    const auto* loadout = world.tryGet<Loadout>(owner);
    if (!loadout || !world.has<PlayerTag>(owner)) return nullptr;
    const auto* tree = world.tryGet<PlayerSkillTree>(owner);
    const int active = tree ? tree->skills.loadoutSlots() : kLoadoutActiveSlots;
    const auto index = registry.petalIndex(std::string(id));
    if (index == kInvalidIndex) return nullptr;
    for (int i = 0; i < active; ++i) {
        if (!isHyperPetal(loadout->slots[static_cast<std::size_t>(i)].rarity)) continue;
        const auto slot = equippedPetal(registry, *loadout, i);
        if (slot.configIndex == index && isHyperPetal(slot.reloadRarity)) {
            return hyperTechniqueInfo(id, Rarity::Apex);
        }
    }
    return nullptr;
}

} // namespace flix

FLIX_COMPONENT(flix::HyperPetalTimers);
