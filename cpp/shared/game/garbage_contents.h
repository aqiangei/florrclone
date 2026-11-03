#pragma once
#include "shared/game/config.h"

namespace flix {
struct GarbagePetal {
    std::uint16_t index = kInvalidIndex;
    std::uint32_t poseSeed = 0;
};

inline std::vector<std::uint16_t> garbageEligiblePetals(const ContentRegistry& content, Rarity rarity) {
    int highest = std::max(1, std::min(rarityIndex(rarity), rarityIndex(Rarity::Super)));
    if (!content.garbageDropRates().empty()) {
        highest = -1;
        for (const auto& rate : ratesFor(content.garbageDropRates(), rarity))
            if (rate.probability > 0) highest = std::max(highest, rarityIndex(rate.rarity));
    }
    std::vector<std::uint16_t> out;
    for (std::uint16_t i = 0; i < content.petalCount(); ++i) {
        const auto& petal = content.petal(i);
        // All playable types, including magic/special-source petals. A lease
        // token is not an ordinary item, and operator-only petals never enter.
        if (petal.isAdminPetal || petal.id == "timed_mjolnir" || rarityIndex(petal.minRarity) > highest) continue;
        const auto& id = petal.id;
        if (id.size() > 4 && id.compare(id.size()-4,4,"_egg") == 0) {
            const auto mob = content.mobIndex(id.substr(0,id.size()-4));
            if (mob != kInvalidIndex && content.mob(mob).noEggDrop) continue;
        }
        out.push_back(i);
    }
    return out;
}

/// The server and every viewer derive the exact same unique types from the
/// authoritative net id. No camera time, local RNG or claimant affects it.
inline std::vector<GarbagePetal> garbageContents(std::vector<std::uint16_t> pool,
                                                std::uint32_t stableId, Vec2 position) {
    const std::uint64_t seed = stableId ? std::uint64_t(stableId) * 104729u
        : static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(position.x/256.0)*1009+std::floor(position.y/256.0)));
    Rng rng{seed ^ 0x4741524241474531ull};
    const auto count = std::min<std::size_t>(pool.size(), 5 + rng.below(5));
    std::vector<GarbagePetal> out;out.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        const auto pick=i+rng.below(static_cast<std::uint32_t>(pool.size()-i));
        std::swap(pool[i],pool[pick]);
        out.push_back({pool[i],static_cast<std::uint32_t>(rng.next())});
    }
    return out;
}
}
