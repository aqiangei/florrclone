#pragma once
#include <vector>
#include "shared/game/rarity.h"
namespace flix {
struct Profile;
struct GalleryDropPreview {
    std::uint16_t petalIndex;
    Rarity rarity;
    double probability;
};
int galleryOrbTier(const Profile&);
std::vector<GalleryDropPreview> galleryDropPreview(std::uint16_t mob, Rarity rarity, int orbTier);
}
