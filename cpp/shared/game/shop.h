#pragma once
// The star shop: what a petal costs, and what may be sold at all.
//
// The price itself is DATA -- `price` on the petal's entry in petals.json, the
// cost of one at the common tier -- and what lives here is the economy around
// it: the rarity ladder, the discount, what is not for sale at any price. It
// used to be a hand-kept table in this file that named thirty of the eighty-
// four petals and left the rest on a default, which meant a new petal was
// priced by forgetting to price it. The shop panel and the purchase handler
// both read the same field, so the client shows a price and the server
// recomputes it and ignores whatever the client claimed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "shared/game/config.h"
#include "shared/game/rarity.h"

namespace flix {

/// Tier prices track the server star bounty; scarce petals retain a premium.
/// Rare missing pieces remain affordable without selling the top reward tiers.
inline double shopPrice(std::uint16_t petalIndex, Rarity rarity) {
    static constexpr double tierPrices[]={3,5,10,20,40,80,360,1800,1800,5400,16200,48600};
    const double base=std::max(1.0,content().petal(petalIndex).price);
    const double affordable=clamp(1.0+std::log(base/20.0)*0.10,0.75,1.50);
    // The previous 1.5 ceiling priced a 99999-base Mimic almost like a basic
    // item. Respect authored acquisition costs without lowering cheap petals.
    double variety=std::max(affordable,std::min(10.0,std::sqrt(base/20.0)));
    if (rarity == Rarity::Ultra || rarity == Rarity::Super) {
        const auto& id = content().petal(petalIndex).id;
        if (id == "golden_leaf") variety = std::max(variety, 5.0);
        if (id == "sawblade") variety = std::max(variety, 8.0);
    }
    const double previous=std::max(1.0,std::floor(tierPrices[clamp(rarityIndex(rarity),0,11)]*variety));
    return 2.0 * std::ceil(previous*1.75);
}

// Apply the economy multiplier after the old discounted rounding too, so
// locked and current offers use the same discounted rounding.
inline double shopOfferPrice(std::uint16_t index, Rarity rarity, int discount) {
    return 2.0 * std::floor((shopPrice(index, rarity) / 2.0) *
                            (100 - clamp(discount, 0, 30)) / 100.0);
}

/// By id. An id no petal answers to has no price at all and quotes zero --
/// there is no longer a default for it to fall through to, because the load
/// that would have needed one is refused (ContentRegistry::loadFiles).
inline double shopPrice(const std::string& petalId, Rarity rarity) {
    const std::uint16_t index = content().petalIndex(petalId);
    return index == kInvalidIndex ? 0.0 : shopPrice(index, rarity);
}

/// Unique and apex are not for sale at any price: they are the reward for
/// crafting and for killing things, and a star price would make both pointless.
/// Universal is only ever given.
inline bool shopSellsRarity(Rarity rarity) {
    return rarityIndex(rarity) < rarityIndex(Rarity::Unique);
}

/// Admin-only petals never appear in the shop, whatever their price says --
/// and neither does the egg of a mob its config forbids eggs for. Both filters
/// are the browser's (src/shop.ts:466-469): dropping either one lands the
/// catalogue on the wrong number of cards.
inline bool shopSellsPetal(std::uint16_t petalIndex) {
    if (petalIndex >= content().petalCount()) return false;
    const PetalConfig& petal = content().petal(petalIndex);
    if (petal.isAdminPetal) return false;
    // Handed out by a rule of their own: the sacrifice, the crown.
    if (petalHasOwnSource(petal)) return false;
    // One that only comes from a tier the shop does not sell has no card.
    if (!shopSellsRarity(petal.minRarity)) return false;
    constexpr char kEggSuffix[] = "_egg";
    constexpr std::size_t kEggSuffixLen = sizeof(kEggSuffix) - 1;
    if (petal.id.size() > kEggSuffixLen &&
        petal.id.compare(petal.id.size() - kEggSuffixLen, kEggSuffixLen, kEggSuffix) == 0) {
        const std::uint16_t mob =
            content().mobIndex(petal.id.substr(0, petal.id.size() - kEggSuffixLen));
        if (mob != kInvalidIndex && content().mob(mob).noEggDrop) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The rotating store
// ---------------------------------------------------------------------------
//
// The shop does not sell the catalogue. It sells ten cards that change on the
// hour, some of them discounted. The server derives those cards from its clock
// and sends the account's remaining stock and prices to the client. A purchase
// carries only which card was clicked; the server validates the current offer.

/// How long one set of offers stands. The panel counts down to the next
/// rotation against this and the server prices a purchase by the rotation the
/// clock is in, so the number is shared rather than spelled out twice.
inline constexpr std::int64_t kShopRotationSeconds = 3600;

/// Two rows of five. The panel's grid is built to this.
inline constexpr int kShopOfferCount = 10;
inline constexpr int kShopOfferColumns = 5;
// All shop cards have unlimited stock. Prices and available cards still apply.
inline int shopStockLimit(Rarity) { return -1; }

/// Seconds since the epoch: the one clock the offers hang off.
inline std::int64_t shopClockNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Which rotation a moment falls in. Floor division, not the language's
/// truncation: a clock set before 1970 would otherwise give the two hours
/// either side of the epoch the same rotation.
inline std::int64_t shopRotation(std::int64_t unixSeconds) {
    const std::int64_t quotient = unixSeconds / kShopRotationSeconds;
    return (unixSeconds < 0 && quotient * kShopRotationSeconds != unixSeconds) ? quotient - 1
                                                                              : quotient;
}

/// When that rotation ends, in the same seconds-since-epoch.
inline std::int64_t shopRotationEnd(std::int64_t rotation) {
    return (rotation + 1) * kShopRotationSeconds;
}

/// One card in the store.
struct ShopOffer {
    std::uint16_t petalIndex = kNoPetal;
    Rarity rarity = Rarity::Common;
    /// What it costs, discount already taken off. This IS the price: nothing
    /// that spends stars re-derives it from the two fields above.
    double price = 0;
    /// 0, or the 10/20/30 the discounted cards wear on their ribbon.
    int discountPercent = 0;
    int remaining = -1; // -1 = unlimited; server sends the account's remainder
};

/// The ten cards a rotation offers.
///
/// Pure, and deterministic to the star: the same rotation index produces the
/// same cards on every machine that loaded the same petals.json. That is what
/// lets the client price a card the server will agree to.
inline std::vector<ShopOffer> shopOffers(std::int64_t rotation) {
    std::vector<ShopOffer> offers;

    std::vector<std::uint16_t> pool;
    for (const std::uint16_t index : content().petalDisplayOrder()) {
        if (shopSellsPetal(index)) pool.push_back(index);
    }
    std::vector<Rarity> tiers;
    for (int tier = 0; tier < kRarityCount; ++tier) {
        const Rarity rarity = clampRarity(tier);
        if (shopSellsRarity(rarity)) tiers.push_back(rarity);
    }
    if (pool.empty() || tiers.empty()) return offers;

    Rng rng(static_cast<std::uint64_t>(rotation));
    const int count = std::min<int>(kShopOfferCount, static_cast<int>(pool.size()));
    const int half = static_cast<int>(tiers.size()) / 2;

    for (int slot = 0; slot < count; ++slot) {
        // Partial Fisher-Yates, so no petal is offered twice in one rotation.
        const auto taken = static_cast<std::size_t>(slot);
        const auto remaining = static_cast<std::uint32_t>(pool.size() - taken);
        std::swap(pool[taken], pool[taken + rng.below(remaining)]);
        // Super-only petals (e.g. Spaceship) belong in the two Super cards.
        // Lifting one of the other eight cards to its floor would silently
        // create a third Super and break the fixed stock guarantee.
        if(slot<count-2&&rarityIndex(content().petal(pool[taken]).minRarity)>=rarityIndex(Rarity::Super)){
            const auto allowed=std::find_if(pool.begin()+taken+1,pool.end(),[](std::uint16_t index){
                return rarityIndex(content().petal(index).minRarity)<rarityIndex(Rarity::Super);
            });
            if(allowed==pool.end())continue;
            std::iter_swap(pool.begin()+taken,allowed);
        }

        // The top row draws from the cheap half of the ladder and the bottom
        // row from the dear half, which is what gives the grid a shape a
        // player can read at a glance instead of ten prices in no order.
        const bool dear = slot >= kShopOfferColumns && half > 0;
        const int lowest = dear ? half : 0;
        const int highest = dear ? std::min(static_cast<int>(tiers.size()) - 1,
                                            rarityIndex(Rarity::Ultra)) : std::max(0, half - 1);

        ShopOffer offer;
        offer.petalIndex = pool[taken];
        offer.rarity = tiers[static_cast<std::size_t>(rng.rangeInt(lowest, highest))];
        // Regular stock now starts at Ultra; retain the two fixed Super slots.
        offer.rarity = Rarity::Ultra;
        // Two fixed Super positions; the other eight cards roll below Super.
        if (slot >= count - 2 && shopSellsRarity(Rarity::Super)) offer.rarity = Rarity::Super;
        // A petal that only comes from some tier up is offered there at the
        // least. Lifted after the draw, so the rotation's stream -- and every
        // other card in it -- is what it was.
        const Rarity floor = content().petal(offer.petalIndex).minRarity;
        if (rarityIndex(offer.rarity) < rarityIndex(floor)) offer.rarity = floor;
        offer.discountPercent = rng.chance(0.2) ? 10 * (1 + static_cast<int>(rng.below(3))) : 0;
        offer.price = shopOfferPrice(offer.petalIndex, offer.rarity, offer.discountPercent);
        offer.remaining = shopStockLimit(offer.rarity);
        offers.push_back(offer);
    }
    return offers;
}

// ---------------------------------------------------------------------------
// Star challenges
// ---------------------------------------------------------------------------

/// Stars awarded for killing a mob of this tier. Mythic is the floor: below it
/// a kill is worth XP and loot only, which is what makes a star mean something.
inline int starsForKill(Rarity mobRarity) {
    switch (mobRarity) {
        case Rarity::Mythic: return 1;
        case Rarity::Ultra:  return 5;
        case Rarity::Super:  return 25;
        case Rarity::Unique: return 100;
        case Rarity::Apex:   return 250;
        default: return 0;
    }
}

} // namespace flix
