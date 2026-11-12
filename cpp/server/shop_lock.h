#pragma once
#include "server/db.h"
#include "shared/game/shop.h"

namespace flix {
inline std::string shopStockKey(std::uint16_t petal, Rarity rarity) {
    return content().petal(petal).id + "@" + rarityName(rarity);
}
inline int shopStockRemaining(const PlayerRecord& record, std::int64_t rotation,
                              std::uint16_t petal, Rarity rarity) {
    const int limit = shopStockLimit(rarity);
    if (limit < 0) return -1;
    const int bought = record.extra["shopPurchases"][std::to_string(rotation)]
        [shopStockKey(petal, rarity)].asInt();
    return std::max(0, limit - std::max(0, bought));
}
inline void consumeShopStock(PlayerRecord& record, std::int64_t rotation,
                             std::uint16_t petal, Rarity rarity, int quantity) {
    if (shopStockLimit(rarity) < 0) return;
    const auto current = std::to_string(shopRotation(shopClockNow()));
    const auto locked = std::to_string(static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1)));
    const auto buying = std::to_string(rotation);
    Json kept = Json::object();
    for (const auto& entry : record.extra["shopPurchases"].members()) {
        if (entry.first == current || entry.first == locked || entry.first == buying) kept[entry.first] = entry.second;
    }
    auto& row = kept[buying][shopStockKey(petal, rarity)];
    row = std::max(0, row.asInt()) + quantity;
    record.extra["shopPurchases"] = std::move(kept);
}
inline void applyShopStock(const PlayerRecord& record, std::int64_t rotation, std::vector<ShopOffer>& offers) {
    for (auto& offer : offers) offer.remaining = shopStockRemaining(record, rotation, offer.petalIndex, offer.rarity);
}
// Store stable content IDs, never registry indices or prices. Locked stock stays
// the same through content updates; prices still follow the current economy.
inline void captureShopLock(PlayerRecord& record, std::int64_t rotation) {
    Json rows=Json::array();
    for(const auto& offer:shopOffers(rotation)) {
        Json row=Json::object();row["petalType"]=content().petal(offer.petalIndex).id;
        row["rarity"]=rarityName(offer.rarity);row["discountPercent"]=offer.discountPercent;
        rows.push(row);
    }
    record.extra["lockedShopOffers"]=rows;
    record.extra["lockedShopOffersRotation"]=static_cast<double>(rotation);
}
inline std::vector<ShopOffer> lockedShopOffers(const PlayerRecord& record) {
    const auto rotation=static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1));
    if(rotation<0)return {};
    const auto& rows=record.extra["lockedShopOffers"];
    if(!rows.isArray() || record.extra["lockedShopOffersRotation"].asDouble(-1)!=rotation)
        return shopOffers(rotation); // Legacy state is captured on login.
    std::vector<ShopOffer> result;
    const auto alreadyListed = [&](std::uint16_t petalIndex) {
        return std::any_of(result.begin(), result.end(), [&](const ShopOffer& offer) {
            return offer.petalIndex == petalIndex;
        });
    };
    for (const auto& row : rows.items()) {
        if (result.size() == kShopOfferCount) break;
        if (row["rarity"].asString() != rarityName(Rarity::Super)) continue;
        const auto petalIndex = content().petalIndex(row["petalType"].asString());
        if (!shopSellsPetal(petalIndex) || alreadyListed(petalIndex)) continue;
        const int discount = clamp(row["discountPercent"].asInt(), 0, 30);
        result.push_back({petalIndex, Rarity::Super,
                          shopOfferPrice(petalIndex, Rarity::Super, discount), discount});
    }
    // Older locks contain Ultra offers. Fill their empty slots from the same
    // hour while preserving any saved Super offers and their discounts.
    if (result.size() < kShopOfferCount) {
        for (const auto& offer : shopOffers(rotation)) {
            if (!alreadyListed(offer.petalIndex)) result.push_back(offer);
            if (result.size() == kShopOfferCount) break;
        }
    }
    return result;
}
} // namespace flix
