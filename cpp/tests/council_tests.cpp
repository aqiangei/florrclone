#include "test.h"
#include "server/council.h"
#include "server_harness.h"
#include <cstdio>
using namespace flix;
namespace { std::int64_t councilNow=1000000; std::int64_t councilClock(){return councilNow;} }
TEST(council_weights_votes_snapshot_and_persistence) {
    const std::string path="/tmp/florr-council-test.json"; std::remove(path.c_str());
    Database db;std::string error;CHECK(db.load(path,error));db.setClock(councilClock);
    auto a=db.createUser("councilAlice","password123");auto b=db.createUser("councilBob","password123");auto admin=db.createUser("councilAdmin","password123");
    CHECK(a.ok());CHECK(b.ok());CHECK(admin.ok());
    a.account->admin=false;b.account->admin=false;admin.account->admin=true;
    const auto aid=a.account->id,bid=b.account->id;
    db.progress(aid).totalXp=1000;db.progress(bid).totalXp=1000;
    db.progress(aid).setItemCount(Rarity::Apex,"petal_basic",1);
    auto rows=council::weights(db);CHECK_EQ(rows.size(),2u);
    CHECK_EQ(rows[0]["units"].asInt()+rows[1]["units"].asInt(),1000000);
    CHECK(rows[0]["units"].asInt()>rows[1]["units"].asInt());
    const auto at=db.createSession(aid,"councilAlice"),mt=db.createSession(admin.account->id,"councilAdmin");
    Json view=Json::object();view["action"]="view";
    auto anonymous=council::request(db,"",view);CHECK_EQ(anonymous["me"].asString(),"");CHECK_EQ(anonymous["players"].size(),2u);
    auto signedIn=council::request(db,at,view);CHECK_EQ(signedIn["me"].asString(),aid);CHECK_EQ(signedIn["accountName"].asString(),"councilAlice");
    auto adminView=council::request(db,mt,view);CHECK_EQ(adminView["accountName"].asString(),"councilAdmin");CHECK(adminView["admin"].asBool());
    Json create=Json::object();create["action"]="create";create["title"]="Balance update";
    CHECK(!council::request(db,mt,create)["error"].isNull());
    auto out=council::request(db,at,create);CHECK(out["error"].isNull());CHECK_EQ(out["polls"].size(),1u);
    Json vote=Json::object();vote["action"]="vote";vote["id"]=out["polls"][0]["id"];vote["choice"]="yes";
    const auto pollId=vote["id"].asString();
    CHECK_EQ(db.storedTable("councilPolls")[pollId]["weightRule"].asInt(),3);
    const std::string frozenSnapshot=db.storedTable("councilPolls")[pollId]["weights"].dump();
    // Simulate an existing v2 poll. Changing rankings or casting votes must not migrate it.
    db.rawTable("councilPolls")[pollId]["weightRule"]=2;
    CHECK(!council::request(db,"invalid",vote)["error"].isNull());
    out=council::request(db,at,vote);const int frozen=out["polls"][0]["yes"].asInt();CHECK(frozen>0);
    db.rawTable("councilActivity")[bid]=10000000;
    vote["choice"]="no";out=council::request(db,at,vote);
    CHECK_EQ(out["polls"][0]["yes"].asInt(),0);CHECK_EQ(out["polls"][0]["no"].asInt(),frozen);CHECK_EQ(out["polls"][0]["count"].asInt(),1);
    CHECK_EQ(db.storedTable("councilPolls")[pollId]["weights"].dump(),frozenSnapshot);
    CHECK_EQ(db.storedTable("councilPolls")[pollId]["weightRule"].asInt(),2);
    CHECK(out["polls"][0]["legacyWeights"].asBool());
    councilNow+=7LL*86400000;CHECK(!council::request(db,at,vote)["error"].isNull());
    CHECK(db.save());Database reloaded;CHECK(reloaded.load(path,error));CHECK_EQ(reloaded.storedTable("councilPolls").size(),1u);
}

TEST(council_empty_and_failed_save_do_not_accept_vote) {
    Database empty;CHECK_EQ(council::weights(empty).size(),0u);
    Database db;std::string error;CHECK(db.load("/tmp/florr-council-no-directory/db.json",error));
    auto a=db.createUser("councilFailure","password123");CHECK(a.ok());a.account->admin=false;
    const auto id=a.account->id;db.progress(id).totalXp=1000;
    auto token=db.createSession(id,a.account->username);
    Json p=Json::object();p["title"]="rollback";p["endsAt"]=static_cast<double>(db.nowMillis())+100000;
    p["weights"]=council::weights(db);p["votes"]=Json::object();db.rawTable("councilPolls")["1"]=p;
    Json vote=Json::object();vote["action"]="vote";vote["id"]="1";vote["choice"]="yes";
    CHECK(!council::request(db,token,vote)["error"].isNull());
    CHECK_EQ(db.storedTable("councilPolls")["1"]["votes"].size(),0u);
}

TEST(council_publish_checks_top_ten_and_rate_limits) {
    const std::string path="/tmp/florr-council-ranks-test.json";std::remove(path.c_str());
    Database db;std::string error;CHECK(db.load(path,error));db.setClock(councilClock);councilNow=2000000;
    std::vector<std::string> ids,tokens;
    for(int i=0;i<11;++i){auto a=db.createUser("rankPlayer"+std::to_string(i),"password123");CHECK(a.ok());a.account->admin=false;ids.push_back(a.account->id);tokens.push_back(db.createSession(a.account->id,a.account->username));db.progress(a.account->id).totalXp=10000000.0/(i+1);}
    Json view=Json::object();view["action"]="view";
    CHECK(council::request(db,tokens[9],view)["canPublish"].asBool());
    CHECK(!council::request(db,tokens[10],view)["canPublish"].asBool());
    Json create=Json::object();create["action"]="create";create["title"]="Top ten proposal";
    CHECK(!council::request(db,tokens[10],create)["error"].isNull());
    CHECK(council::request(db,tokens[9],create)["error"].isNull());
    CHECK(!council::request(db,tokens[9],create)["error"].isNull());
    councilNow+=86400000;CHECK(council::request(db,tokens[9],create)["error"].isNull());
    councilNow+=86400000;CHECK(council::request(db,tokens[9],create)["error"].isNull());
    councilNow+=86400000;CHECK(council::request(db,tokens[9],create)["error"].isNull());
    db.progress(ids[10]).totalXp=100000000;
    CHECK(!council::request(db,tokens[9],view)["canPublish"].asBool());
    CHECK(!council::request(db,tokens[9],create)["error"].isNull());
    CHECK(council::request(db,tokens[10],create)["error"].isNull());
    auto guest=db.findUserById(ids[10]);guest->guest=true;
    CHECK(!council::request(db,tokens[10],create)["error"].isNull());
}

TEST(council_cold_start_does_not_give_one_early_visitor_forty_percent) {
    Database db;std::string error;const std::string path="/tmp/florr-council-cold.json";std::remove(path.c_str());CHECK(db.load(path,error));db.setPasswordCost(4);
    std::string first;
    for(int i=0;i<40;++i){auto a=db.createUser("cold"+std::to_string(i),"password123");CHECK(a.ok());a.account->admin=false;db.progress(a.account->id).totalXp=1000000;if(i==0)first=a.account->id;}
    db.rawTable("councilActivity")[first]=60;
    auto rows=council::weights(db);int total=0;for(const auto& row:rows.items()){total+=row["units"].asInt();CHECK(row["units"].asInt()<26000);}CHECK_EQ(total,1000000);
}

TEST(council_legacy_level_credit_is_once_only_and_separate_from_real_time) {
    Database db;std::string error;const std::string path="/tmp/florr-council-legacy.json";std::remove(path.c_str());CHECK(db.load(path,error));
    auto a=db.createUser("legacyPlayer","password123"),m=db.createUser("legacyAdmin","password123");CHECK(a.ok());CHECK(m.ok());a.account->admin=false;m.account->admin=true;
    const auto id=a.account->id;db.progress(id).totalXp=10000000;db.rawTable("councilActivity")[id]=120;
    CHECK(council::initializeLegacyActivity(db));const auto saved=db.storedTable("councilLegacyActivity");
    const int level=levelFromTotalXp(db.progress(id).totalXp).level;const double credit=9.0*(level-1)*(level-1);
    CHECK_NEAR(saved["accounts"][id]["seconds"].asDouble(),credit,0.001);CHECK(saved["accounts"][m.account->id].isNull());
    db.progress(id).totalXp=1e12;auto b=db.createUser("newPlayerLater","password123");b.account->admin=false;db.progress(b.account->id).totalXp=10000000;
    CHECK(council::initializeLegacyActivity(db));CHECK_EQ(db.storedTable("councilLegacyActivity").dump(),saved.dump());
    const auto rows=council::weights(db);for(const auto& row:rows.items())if(row["id"].asString()==id){CHECK_NEAR(row["recordedSeconds"].asDouble(),120,0.001);CHECK_NEAR(row["creditedSeconds"].asDouble(),credit,0.001);}
    db.markDirty();CHECK(db.save());Database reload;CHECK(reload.load(path,error));CHECK(council::initializeLegacyActivity(reload));CHECK_EQ(reload.storedTable("councilLegacyActivity").dump(),saved.dump());
}

TEST(council_hyper_details_keep_history_without_presets_or_extra_copies) {
    Database db;std::string error;const std::string path="/tmp/florr-council-history.json";std::remove(path.c_str());CHECK(db.load(path,error));db.setClock(councilClock);councilNow=1000000000;
    auto a=db.createUser("historyPlayer","password123");CHECK(a.ok());a.account->admin=false;const auto id=a.account->id;
    auto& p=db.progress(id);p.setItemCount(Rarity::Apex,"petal_rose",2);p.loadout.resize(1);StoredItem worn;worn.rarity=Rarity::Apex;worn.petalType="egg";p.loadout[0]=worn;p.loadoutPresets["K1"]=p.loadout;
    CHECK(db.save());Json input=Json::object();input["action"]="player";input["id"]=id;
    auto out=council::request(db,"",input);CHECK_EQ(out["current"].size(),2u);CHECK_EQ(out["history"].size(),2u);
    int count=0;for(const auto& row:out["current"].items())count+=row["count"].asInt();CHECK_EQ(count,3);
    // Table caches serialized rows after save. Reacquire the mutable record,
    // as production mutations must, to invalidate that row before removing its last H.
    auto& updated=db.progress(id);updated.setItemCount(Rarity::Apex,"petal_rose",0);updated.loadout[0].reset();councilNow+=60000;db.markDirty();CHECK(db.save());
    Database reload;CHECK(reload.load(path,error));out=council::request(reload,"",input);CHECK_EQ(out["current"].size(),0u);CHECK_EQ(out["history"].size(),2u);
    CHECK_EQ(reload.findProgress(id)->itemCount(Rarity::Apex,"petal_rose"),0);
    CHECK(!reload.findProgress(id)->loadout[0].has_value());
    CHECK_EQ(reload.findProgress(id)->loadoutPresets.at("K1").size(),1u);
    for(const auto& row:out["history"].items()){CHECK(!row["holding"].asBool());CHECK_EQ(row["firstSeenAt"].asInt64(),1000000000);CHECK_EQ(row["lastSeenAt"].asInt64(),1000000000);}
    CHECK(out["inventory"].isNull());CHECK(out["sessions"].isNull());CHECK(out["passwordHash"].isNull());
}


TEST(council_equal_level_time_budgets_and_low_inventory_budget) {
    Database db;std::string error;const std::string path="/tmp/florr-council-equal.json";std::remove(path.c_str());CHECK(db.load(path,error));db.setPasswordCost(4);
    auto a=db.createUser("equalA","password123"),b=db.createUser("equalB","password123");a.account->admin=false;b.account->admin=false;
    db.progress(a.account->id).totalXp=10000000;db.progress(b.account->id).totalXp=1000000;
    db.progress(b.account->id).setItemCount(Rarity::Apex,"petal_basic",1000);
    db.rawTable("councilActivity")[a.account->id]=3600;db.rawTable("councilActivity")[b.account->id]=3600;
    double level=0,time=0,petals=0;int total=0;
    const auto ranked=council::weights(db);for(const auto& row:ranked.items()) {
        level+=row["levelContribution"].asDouble();time+=row["activityContribution"].asDouble();petals+=row["petalContribution"].asDouble();total+=row["units"].asInt();
        CHECK_NEAR(row["levelScore"].asDouble(),row["level"].asInt()-1,0.00001);
    }
    CHECK_NEAR(level,.45,0.000001);CHECK_NEAR(time,.45,0.000001);CHECK_NEAR(petals,.10,0.000001);CHECK_EQ(total,1000000);
}

TEST(council_activity_credits_real_seconds_at_twenty_tps_and_stops_when_idle) {
    using namespace testsupport;
    const auto dir=twoMapDataDir("council-activity20");
    Harness h("council-activity20",[](const std::string& path){seedUser(path,"activePlayer","password123");},dir,0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient c;CHECK(loginAs(h,c,"activePlayer","password123"));c.joinGame(1280,720);
    CHECK(h.stepUntil({&c},[&]{auto* s=GameServerPeer::sessionOf(h.server,"activePlayer");return s&&s->playing();}));
    auto* session=GameServerPeer::sessionOf(h.server,"activePlayer");if(!session||!session->playing())return;
    session->councilActiveUntil=h.clock+100000;const auto id=session->userId;
    h.step(net::kTicksPerSecond,{&c});const double before=h.server.database().storedTable("councilActivity")[id].asDouble();
    h.step(2*net::kTicksPerSecond,{&c});const double after=h.server.database().storedTable("councilActivity")[id].asDouble();
    CHECK_NEAR(after-before,2,0.001);
    session->councilActiveUntil=h.clock-1;h.step(2*net::kTicksPerSecond,{&c});
    CHECK_NEAR(h.server.database().storedTable("councilActivity")[id].asDouble(),after,0.001);
}
TEST(council_early_close_requires_server_admin_and_freezes_votes) {
 const std::string path="/tmp/florr-council-close-test.json";std::remove(path.c_str());
 Database db;std::string error;CHECK(db.load(path,error));db.setClock(councilClock);
 auto a=db.createUser("aqa","password123");const auto aid=a.account->id;a.account->admin=false;
 auto m=db.createUser("operator","password123");const auto mid=m.account->id;m.account->admin=true;
 db.progress(aid).totalXp=1000;
 const auto at=db.createSession(aid,"aqa"),mt=db.createSession(mid,"operator");
 Json create=Json::object();create["action"]="create";create["title"]="Close test";
 auto out=council::request(db,at,create);CHECK(out["error"].isNull());
 Json close=Json::object();close["action"]="close";close["id"]=out["polls"][0]["id"];
 CHECK(!council::request(db,at,close)["error"].isNull());
 out=council::request(db,mt,close);CHECK(out["error"].isNull());CHECK(out["polls"][0]["closed"].asBool());
 CHECK_EQ(out["polls"][0]["result"].asString(),"tied");
 close["action"]="vote";close["choice"]="yes";CHECK(!council::request(db,at,close)["error"].isNull());
 std::remove(path.c_str());
}
