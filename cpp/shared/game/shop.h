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
#include <iterator>
#include <string>
#include <vector>

#include "shared/game/config.h"
#include "shared/game/rarity.h"
#include "shared/game/shop_prices.h"

namespace flix {

/// Tier prices track the server star bounty; scarce petals retain a premium.
/// Rare missing pieces remain affordable without selling the top reward tiers.
inline double legacyShopPrice(std::uint16_t petalIndex, Rarity rarity) {
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
    return 2.0 * std::ceil(previous * 1.25);
}

inline double shopPrice(std::uint16_t index, Rarity rarity) {
    if (rarity != Rarity::Super && rarity != Rarity::Unique)
        return legacyShopPrice(index, rarity);
    const std::string_view id = content().petal(index).id;
    const auto* end = std::end(kShopSuperPrices);
    const auto* row = std::lower_bound(std::begin(kShopSuperPrices), end, id,
        [](const ShopSuperPrice& entry, std::string_view key) { return entry.id < key; });
    const double super = row != end && row->id == id ? row->stars : legacyShopPrice(index, Rarity::Super);
    // Direct Eternal purchase costs more than the Titan's repeat 15-Super exchange.
    return rarity == Rarity::Unique ? 18.0 * super : super;
}

inline double shopOfferPrice(std::uint16_t index, Rarity rarity, int discount) {
    if (rarity == Rarity::Unique) return shopPrice(index, rarity);
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

/// The rotating shop sells through Eternal.
/// Ultra is no longer sold. Lower tiers retain their legacy direct-buy prices.
inline bool shopSellsRarity(Rarity rarity) {
    return rarity != Rarity::Ultra && rarityIndex(rarity) <= rarityIndex(Rarity::Unique);
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
    if (!petalComesAt(petal, Rarity::Super)) return false;
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
// The shop offers five Super and five Eternal cards that change on the
// hour. Some Super cards are discounted. The server derives those cards from its clock
// and sends the current offers and prices to the client. A purchase
// carries only which card was clicked; the server validates the current offer.

/// How long one set of offers stands. The panel counts down to the next
/// rotation against this and the server prices a purchase by the rotation the
/// clock is in, so the number is shared rather than spelled out twice.
inline constexpr std::int64_t kShopRotationSeconds = 3600;

inline constexpr int kShopTierOfferCount = 5;
inline constexpr int kShopOfferCount = 2 * kShopTierOfferCount;

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

/// The hourly selection, shared by all servers.
inline std::vector<ShopOffer> shopOffers(std::int64_t rotation) {
    std::vector<std::uint16_t> pool;
    for (const std::uint16_t index : content().petalDisplayOrder()) {
        if (shopSellsPetal(index)) pool.push_back(index);
    }

    Rng rng(static_cast<std::uint64_t>(rotation));
    std::vector<ShopOffer> offers;
    const auto count = std::min<std::size_t>(kShopTierOfferCount, pool.size());
    for (std::size_t slot = 0; slot < count; ++slot) {
        // Partial Fisher-Yates, so no petal is offered twice in one rotation.
        const auto remaining = static_cast<std::uint32_t>(pool.size() - slot);
        std::swap(pool[slot], pool[slot + rng.below(remaining)]);

        ShopOffer offer;
        offer.petalIndex = pool[slot];
        offer.rarity = Rarity::Super;
        offer.discountPercent = rng.chance(0.2) ? 10 * (1 + static_cast<int>(rng.below(3))) : 0;
        offer.price = shopOfferPrice(offer.petalIndex, offer.rarity, offer.discountPercent);
        offers.push_back(offer);
    }
    pool.erase(pool.begin(), pool.begin() + count);
    pool.erase(std::remove_if(pool.begin(), pool.end(), [](std::uint16_t petal) {
        return !petalComesAt(content().petal(petal), Rarity::Unique);
    }), pool.end());
    const auto eternalCount = std::min<std::size_t>(kShopTierOfferCount, pool.size());
    for (std::size_t slot = 0; slot < eternalCount; ++slot) {
        const auto remaining = static_cast<std::uint32_t>(pool.size() - slot);
        std::swap(pool[slot], pool[slot + rng.below(remaining)]);
        const auto petal = pool[slot];
        offers.push_back({petal, Rarity::Unique, shopPrice(petal, Rarity::Unique),
                          0, -1});
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
