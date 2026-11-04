#include "test.h"
#include "server/hyper_board.h"
#include "server_harness.h"
using namespace flix;
using namespace flix::testsupport;

TEST(hyper_board_counts_bag_equipment_claims_and_ignores_history) {
    Database db; db.setPasswordCost(4);
    const auto created = db.createUser("holder", "password7"); CHECK(created.ok());
    const auto id = created.account->id;
    auto& r = db.progress(id); r.inventory = Json::object(); r.loadout.clear();
    r.addItem(Rarity::Apex, "petal_rock", 1);
    StoredItem worn; worn.type="petal"; worn.petalType="rock"; worn.rarity=Rarity::Apex;
    r.loadout.push_back(worn); r.loadoutPresets["saved"].push_back(worn);
    r.addItem(Rarity::Unique, "petal_leaf", 10);
    r.addItem(Rarity::Apex, "petal_mjolnir", 1);
    r.extra["temporaryHyperHammer"]["active"]=true;
    r.extra["temporaryHyperHammer"]["expiresAt"]=1;
    db.rawTable("hyperForges")["rock"]["owner"]=id;
    db.rawTable("hyperForges")["garden:rock"]["owner"]=id;
    db.rawTable("hyperForges")["corn"]["owner"]=id;
    db.rawTable("hyperForges")["leaf"]["owner"]="";
    const auto rows=hyperHoldingBoard(db,100);
    CHECK_EQ(rows.size(),std::size_t(2));
    CHECK_EQ(rows[0].petal,std::string("corn")); CHECK_EQ(rows[0].count,1u);
    CHECK_EQ(rows[1].petal,std::string("rock")); CHECK_EQ(rows[1].count,2u);
    CHECK_EQ(rows[0].ownerTotal,3u);
    CHECK_EQ(db.findProgress(id)->itemCount(Rarity::Apex,"petal_mjolnir"),1);
}

TEST(hyper_board_roundtrip_includes_offline_admins_and_pages_every_holding) {
    Harness h("hyper-board", [](const std::string& path) {
        Database db; std::string error; db.load(path,error); db.setPasswordCost(4);
        auto a=db.createUser("viewer","password7"); CHECK(a.ok());
        auto b=db.createUser("offline","password7"); CHECK(b.ok());
        const auto id=b.account->id;
        db.findUser("offline")->admin=true;
        auto& r=db.progress(id); r.inventory=Json::object(); r.loadout.clear();
        for(int i=0;i<105;++i)r.addItem(Rarity::Apex,"petal_test_"+std::to_string(i),1);
        db.markDirty(); CHECK(db.save());
    });
    CHECK(h.ready); if(!h.ready)return;
    NetClient c; CHECK(connectClient(h,c)); c.requestLogin("viewer","password7");
    CHECK(h.stepUntil({&c},[&]{return c.status()==NetClient::Status::LoggedIn;}));
    c.requestLeaderboard(false);
    CHECK(h.stepUntil({&c},[&]{return !c.leaderboardPending();}));
    CHECK_EQ(c.hyperHoldingsTotal(),105u); CHECK_EQ(c.hyperBoard().size(),std::size_t(100));
    CHECK_EQ(c.hyperBoard()[0].name,std::string("offline")); CHECK_EQ(c.hyperBoard()[0].ownerTotal,105u);
    const auto last=c.hyperBoard().back().petal;
    c.requestLeaderboard(false,100);
    CHECK(h.stepUntil({&c},[&]{return !c.leaderboardPending();}));
    CHECK_EQ(c.hyperBoard().size(),std::size_t(5)); CHECK_EQ(c.hyperHoldingsOffset(),100u);
    CHECK(c.hyperBoard()[0].petal>last);
    c.requestLeaderboard(false,UINT32_MAX);
    CHECK(h.stepUntil({&c},[&]{return !c.leaderboardPending();}));
    CHECK_EQ(c.hyperHoldingsOffset(),100u);
}
