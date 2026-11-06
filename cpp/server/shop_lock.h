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
    for(const auto& row:rows.items()) {
        ShopOffer offer;offer.petalIndex=content().petalIndex(row["petalType"].asString());
        offer.rarity=parseRarity(row["rarity"].asString());
        if(offer.petalIndex==kInvalidIndex || !shopSellsPetal(offer.petalIndex) || !shopSellsRarity(offer.rarity))continue;
        offer.discountPercent=clamp(row["discountPercent"].asInt(),0,30);
        offer.price=shopOfferPrice(offer.petalIndex,offer.rarity,offer.discountPercent);
        result.push_back(offer);
    }
    return result;
}
} // namespace flix
