#include "test.h"
#include "server_harness.h"
#include "server/shop_lock.h"
using namespace flix;
using namespace flix::testsupport;

TEST(shop_stock_locked_moon_rock_uses_the_new_discounted_price) {
    const auto rotation = shopRotation(shopClockNow()) - 1;
    Harness h("shop-moon-rock", [&](const std::string& path) {
        seedUser(path, "buyer", "password");
        Database db;
        std::string error;
        CHECK(db.load(path, error));
        auto& record = db.progress(db.findUser("buyer")->id);
        record.stars = 35000;
        record.extra["lockedShopRotation"] = static_cast<double>(rotation);
        record.extra["lockedShopOffersRotation"] = static_cast<double>(rotation);
        Json row = Json::object();
        row["petalType"] = "moon_rock";
        row["rarity"] = rarityName(Rarity::Super);
        row["discountPercent"] = 30;
        row["price"] = 3982;  // An old cached seven-tenths price.
        record.extra["lockedShopOffers"] = Json::array();
        record.extra["lockedShopOffers"].push(row);
        db.markDirty();
        CHECK(db.save());
    }, dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient buyer;
    CHECK(loginAs(h, buyer, "buyer", "password"));
    CHECK(h.stepUntil({&buyer}, [&] { return buyer.lockedShopOffers.size() == 1; }));
    if (buyer.lockedShopOffers.size() != 1) return;
    const auto moon = content().petalIndex("moon_rock");
    CHECK_EQ(buyer.lockedShopOffers.front().petalIndex, moon);
    CHECK_NEAR(buyer.lockedShopOffers.front().price, 31500.0, 1e-9);
    const double starsBefore = buyer.profile().stars;
    buyer.requestBuyPetal(moon, Rarity::Super, 0, 1);
    CHECK(h.stepUntil({&buyer}, [&] { return buyer.shopOutcome().pending; }));
    CHECK(buyer.shopOutcome().ok);
    CHECK_EQ(buyer.profile().stackCount(moon, Rarity::Super), 1);
    CHECK_NEAR(buyer.profile().stars, starsBefore - 31500.0, 1e-9);
    buyer.shopOutcome().pending = false;
    buyer.requestBuyPetal(moon, Rarity::Super, 0, 1);
    CHECK(h.stepUntil({&buyer}, [&] { return buyer.shopOutcome().pending; }));
    CHECK(!buyer.shopOutcome().ok);
    CHECK_EQ(buyer.profile().stackCount(moon, Rarity::Super), 1);
}

TEST(shop_stock_unlimited_locked_offers_ignore_old_sold_out_records_and_keep_prices) {
    const auto rotation=shopRotation(shopClockNow())-1;
    Harness h("shop-unlimited-lock",[&](const std::string& path){
        seedUser(path,"buyer","password");
        Database db;std::string error;CHECK(db.load(path,error));
        auto& r=db.progress(db.findUser("buyer")->id);r.stars=1000000000;
        r.extra["lockedShopRotation"]=static_cast<double>(rotation);
        r.extra["lockedShopOffersRotation"]=static_cast<double>(rotation);
        Json offers=Json::array();
        for(const auto rarity:{Rarity::Super,Rarity::Ultra}){
            Json row=Json::object();row["petalType"]="basic";
            row["rarity"]=rarityName(rarity);row["discountPercent"]=10;offers.push(row);
            for(const auto at:{rotation,rotation+1})
                r.extra["shopPurchases"][std::to_string(at)][std::string("basic@")+rarityName(rarity)]=999;
        }
        r.extra["lockedShopOffers"]=offers;db.markDirty();CHECK(db.save());
    },dataDir(),0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient buyer;CHECK(loginAs(h,buyer,"buyer","password"));
    CHECK(h.stepUntil({&buyer},[&]{return buyer.lockedShopOffers.size()==2;}));
    if(buyer.lockedShopOffers.size()!=2)return;
    const auto basic=content().petalIndex("basic");
    auto buy=[&](Rarity rarity,int slot,int quantity,bool expected){
        buyer.shopOutcome().pending=false;
        buyer.requestBuyPetal(basic,rarity,slot,quantity);
        CHECK(h.stepUntil({&buyer},[&]{return buyer.shopOutcome().pending;}));
        CHECK_EQ(buyer.shopOutcome().ok,expected);
    };
    double expectedStars=buyer.profile().stars;
    for(int slot=0;slot<2;++slot){
        const auto rarity=slot==0?Rarity::Super:Rarity::Ultra;
        CHECK_EQ(buyer.lockedShopOffers[slot].remaining,-1);
        const double price=shopOfferPrice(basic,rarity,10);
        CHECK_NEAR(buyer.lockedShopOffers[slot].price,price,1e-9);
        buy(rarity,slot,6,true);buy(rarity,slot,6,true);
        expectedStars-=12*price;
        CHECK_EQ(buyer.profile().stackCount(basic,rarity),12);
        CHECK_NEAR(buyer.profile().stars,expectedStars,1e-9);
        CHECK_EQ(buyer.lockedShopOffers[slot].remaining,-1);
        buy(rarity,net::kNoShopOffer,1,false);
        buy(rarity,9,1,false);
        buy(rarity,slot,10001,false);
        CHECK_NEAR(buyer.profile().stars,expectedStars,1e-9);
    }
    buyer.disconnect();h.step(3,{});
    NetClient returning;CHECK(loginAs(h,returning,"buyer","password"));
    CHECK(h.stepUntil({&returning},[&]{return returning.lockedShopOffers.size()==2;}));
    if(returning.lockedShopOffers.size()!=2)return;
    CHECK_EQ(returning.lockedShopOffers[0].remaining,-1);
    CHECK_EQ(returning.profile().stackCount(basic,Rarity::Super),12);
    CHECK_NEAR(returning.profile().stars,expectedStars,1e-9);
    h.server.persistAll();
    Database saved;std::string error;CHECK(saved.load(h.dbPath,error));
    const auto* user=saved.findUser("buyer");CHECK(user!=nullptr);if(!user)return;
    const auto* record=saved.findProgress(user->id);CHECK(record!=nullptr);if(!record)return;
    CHECK_EQ(shopStockRemaining(*record,rotation,basic,Rarity::Super),-1);
    CHECK_EQ(record->extra["shopPurchases"][std::to_string(rotation)][std::string("basic@")+rarityName(Rarity::Super)].asInt(),999);
}

TEST(shop_stock_current_offers_unlimited_but_still_require_stars) {
    Harness h("shop-unlimited-current",[](const std::string& path){
        seedUser(path,"buyer","password");
        seedUser(path,"poor","password");
        Database db;std::string error;CHECK(db.load(path,error));
        db.progress(db.findUser("buyer")->id).stars=1000000000;
        db.markDirty();CHECK(db.save());
    },dataDir(),0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient buyer;CHECK(loginAs(h,buyer,"buyer","password"));
    CHECK(h.stepUntil({&buyer},[&]{return !buyer.currentShopOffers.empty();}));
    if(buyer.currentShopOffers.empty())return;
    for(const auto& offer:buyer.currentShopOffers)CHECK_EQ(offer.remaining,-1);
    const auto slot=buyer.currentShopOffers.size()-1;
    const auto offer=buyer.currentShopOffers[slot];
    const auto before=buyer.profile().stackCount(offer.petalIndex,offer.rarity);
    buyer.requestBuyPetal(offer.petalIndex,offer.rarity,slot,6);
    CHECK(h.stepUntil({&buyer},[&]{return buyer.shopOutcome().pending;}));
    CHECK(buyer.shopOutcome().ok);
    CHECK_EQ(buyer.profile().stackCount(offer.petalIndex,offer.rarity),before+6);
    NetClient poor;CHECK(loginAs(h,poor,"poor","password"));
    const auto poorStars=poor.profile().stars;
    CHECK(poorStars<offer.price);
    const auto poorBefore=poor.profile().stackCount(offer.petalIndex,offer.rarity);
    poor.requestBuyPetal(offer.petalIndex,offer.rarity,slot,1);
    CHECK(h.stepUntil({&poor},[&]{return poor.shopOutcome().pending;}));
    CHECK(!poor.shopOutcome().ok);
    CHECK_EQ(poor.profile().stars,poorStars);
    CHECK_EQ(poor.profile().stackCount(offer.petalIndex,offer.rarity),poorBefore);
    CHECK_EQ(buyer.profile().stackCount(offer.petalIndex,offer.rarity),before+6);
}

TEST(shop_stock_regular_rotation_has_eight_ultras_and_two_supers) {
    Harness h("shop-stock-tiers");
    for (std::uint64_t rotation = 100; rotation < 200; ++rotation) {
        const auto offers = shopOffers(rotation);
        CHECK_EQ(offers.size(), std::size_t(10));
        int supers = 0;
        for (const auto& offer : offers) {
            CHECK(offer.rarity == Rarity::Ultra || offer.rarity == Rarity::Super);
            if (offer.rarity == Rarity::Super) ++supers;
        }
        CHECK_EQ(supers, 2);
    }
}
