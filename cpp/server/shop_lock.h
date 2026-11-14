#pragma once
#include "server/db.h"
#include "shared/game/shop.h"

namespace flix {
// Store stable content IDs, never registry indices or prices. Locked stock stays
// the same through content updates; prices still follow the current economy.
inline void captureShopLock(PlayerRecord& record, std::int64_t rotation) {
    Json rows=Json::array();
    for(const auto& offer:shopOffers(rotation)) {
        if (offer.rarity != Rarity::Super) continue;
        Json row=Json::object();row["petalType"]=content().petal(offer.petalIndex).id;
        row["rarity"]=rarityName(offer.rarity);row["discountPercent"]=offer.discountPercent;
        rows.push(row);
    }
    record.extra["lockedShopOffers"]=rows;
    record.extra["lockedShopOffersRotation"]=static_cast<double>(rotation);
}
inline std::vector<ShopOffer> lockedShopOffers(const PlayerRecord& record,
                                               std::int64_t currentRotation = shopRotation(shopClockNow())) {
    const auto rotation=static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1));
    if(rotation<0)return {};
    const auto current = shopOffers(currentRotation);
    const auto& rows=record.extra["lockedShopOffers"];
    const bool captured = rows.isArray() && record.extra["lockedShopOffersRotation"].asDouble(-1) == rotation;
    std::vector<ShopOffer> result;
    const auto alreadyListed = [&](std::uint16_t petalIndex) {
        return std::any_of(result.begin(), result.end(), [&](const ShopOffer& offer) {
            return offer.petalIndex == petalIndex;
        });
    };
    if (captured) for (const auto& row : rows.items()) {
        if (result.size() == kShopTierOfferCount) break;
        if (row["rarity"].asString() != rarityName(Rarity::Super)) continue;
        const auto petalIndex = content().petalIndex(row["petalType"].asString());
        if (!shopSellsPetal(petalIndex) || alreadyListed(petalIndex)) continue;
        const int discount = clamp(row["discountPercent"].asInt(), 0, 30);
        result.push_back({petalIndex, Rarity::Super,
                          shopOfferPrice(petalIndex, Rarity::Super, discount), discount});
    }
    // Older locks contain Ultra offers. Fill their empty slots from the same
    // hour while preserving any saved Super offers and their discounts.
    if (result.size() < kShopTierOfferCount) {
        for (const auto& offer : shopOffers(rotation)) {
            if (offer.rarity == Rarity::Super && !alreadyListed(offer.petalIndex)) result.push_back(offer);
            if (result.size() == kShopTierOfferCount) break;
        }
    }
    for (const auto& offer : current) {
        if (offer.rarity == Rarity::Unique) result.push_back(offer);
    }
    return result;
}
} // namespace flix
