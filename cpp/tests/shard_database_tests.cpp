#include "test.h"
#include "server/db.h"
#include <stdexcept>
using namespace flix;

TEST(shard_database_read_projection_applies_committed_rows_without_writes) {
    Database db;std::string error;int writes=0;
    CHECK(db.loadAuthoritative("{\"players\":{\"a\":{\"totalXP\":10}},\"users\":{\"Alice\":{\"id\":\"a\",\"username\":\"Alice\"}}}",
        [&](const std::string&){++writes;return Database::CommitOutcome::Rejected;},error));
    Json delta=Json::object();delta["players"]["a"]["totalXP"]=99;
    delta["users"]["Alice"]=Json();delta["users"]["Renamed"]["id"]="a";
    delta["globals"]["councilPolls"]["poll"]["title"]="kept";
    CHECK(db.applyAuthoritativeDelta(delta));CHECK(!db.dirty());CHECK_EQ(writes,0);
    CHECK(db.findUser("Alice")==nullptr);CHECK(db.findUser("renamed")!=nullptr);
    CHECK_EQ(db.findUserById("a")->username,"Renamed");CHECK_EQ(db.findProgress("a")->totalXp,99);
    CHECK_EQ(db.storedTable("councilPolls")["poll"]["title"].asString(),"kept");
    Json invalid=delta;invalid["players"]["a"]=false;CHECK(!db.applyAuthoritativeDelta(invalid));
    CHECK_EQ(db.findProgress("a")->totalXp,99);
    db.progress("a").totalXp=123;db.markDirty();CHECK(!db.applyAuthoritativeDelta(delta));
    CHECK_EQ(db.findProgress("a")->totalXp,123);CHECK_EQ(writes,0);
}

TEST(shard_database_delta_is_sparse_and_ack_does_not_erase_later_changes) {
    Database db;std::string error;Json initial=Json::object();initial["users"]=Json::object();
    for(int i=0;i<3500;++i){Json row=Json::object();row["totalXP"]=i;initial["players"]["a"+std::to_string(i)]=row;}
    CHECK(db.loadAuthoritative(initial.dump(),[](const std::string&){return Database::CommitOutcome::Rejected;},error));
    Json submitted;int calls=0;auto outcome=Database::CommitOutcome::Pending;
    db.setDeltaSink([&](const Json& delta){submitted=delta;++calls;return outcome;});
    db.progress("a7").totalXp=77;db.markDirty();const auto first=db.mutationGeneration();
    CHECK(!db.save());CHECK(db.commitPending());CHECK_EQ(submitted["players"].size(),1u);
    CHECK_EQ(submitted["players"]["a7"]["totalXP"].asInt(),77);
    CHECK(submitted.dump().size()<2000);
    db.progress("a8").totalXp=88;db.markDirty();
    outcome=Database::CommitOutcome::Committed;
    CHECK(db.save());CHECK(db.dirty());CHECK_EQ(db.committedGeneration(),first);
    CHECK_EQ(submitted["players"].size(),1u);CHECK(!submitted["players"].contains("a8"));
    CHECK(db.save());CHECK(!db.dirty());CHECK_EQ(submitted["players"].size(),1u);
    CHECK_EQ(submitted["players"]["a8"]["totalXP"].asInt(),88);
    CHECK_EQ(db.committedGeneration(),db.mutationGeneration());
    const int before=calls;CHECK(db.save());CHECK_EQ(calls,before);
}

TEST(shard_database_pending_same_row_retains_newer_value_and_raw_table_update) {
    Database db;std::string error;Json submitted;auto outcome=Database::CommitOutcome::Pending;
    CHECK(db.loadAuthoritative("{\"players\":{\"a\":{\"totalXP\":1}},\"users\":{}}",[](const std::string&){return Database::CommitOutcome::Rejected;},error));
    db.setDeltaSink([&](const Json& delta){submitted=delta;return outcome;});
    db.progress("a").totalXp=2;db.markDirty();CHECK(!db.save());
    db.progress("a").totalXp=3;db.rawTable("councilActivity")["a"]=9;db.markDirty();
    outcome=Database::CommitOutcome::Committed;CHECK(db.save());CHECK(db.dirty());
    CHECK_EQ(db.findProgress("a")->totalXp,3);
    CHECK(db.save());CHECK_EQ(submitted["players"]["a"]["totalXP"].asInt(),3);
    CHECK_EQ(submitted["globals"]["councilActivity"]["a"].asInt(),9);
}

TEST(shard_database_routes_real_serialisation_and_retries_rejected_commit) {
    Database db;
    std::string error, committed;
    int calls = 0;
    auto result = Database::CommitOutcome::Rejected;
    CHECK(db.loadAuthoritative("{\"users\":{},\"players\":{},\"unknown\":{\"kept\":42}}",
        [&](const std::string& text) { ++calls; committed=text; return result; }, error));
    db.rawTable("councilActivity")["account"] = 123;
    db.markDirty();
    CHECK(!db.save()); CHECK(db.dirty()); CHECK(!db.commitUncertain());
    result = Database::CommitOutcome::Committed;
    CHECK(db.save()); CHECK(!db.dirty()); CHECK_EQ(calls, 2);
    const auto disk = Json::parseOrNull(committed);
    CHECK_EQ(disk["unknown"]["kept"].asInt(), 42);
    CHECK_EQ(disk["councilActivity"]["account"].asInt(), 123);
    CHECK(!db.load("must-never-be-created.json", error));
    CHECK(db.authoritative());
}

TEST(shard_database_unknown_commit_cannot_retry_or_fall_back) {
    Database db; std::string error; int calls = 0;
    CHECK(db.loadAuthoritative("{}", [&](const std::string&) {
        ++calls; return Database::CommitOutcome::Unknown;
    }, error));
    db.markDirty();
    CHECK(!db.save()); CHECK(db.commitUncertain());
    CHECK(!db.save()); CHECK(!db.maybeSave(10000)); CHECK_EQ(calls, 1);
    CHECK(!db.load("must-never-be-created.json", error));
    CHECK(!db.loadAuthoritative("{}", [](const std::string&) {
        return Database::CommitOutcome::Committed;
    }, error));
}

TEST(shard_database_exception_is_unknown_and_destructor_does_not_send_rpc) {
    int calls = 0;
    {
        Database db; std::string error;
        CHECK(db.loadAuthoritative("{}", [&](const std::string&) -> Database::CommitOutcome {
            ++calls; throw std::runtime_error("transport response lost");
        }, error));
        db.markDirty(); CHECK(!db.save()); CHECK(db.commitUncertain());
    }
    CHECK_EQ(calls, 1);
    {
        Database db; std::string error;
        CHECK(db.loadAuthoritative("{}", [&](const std::string&) {
            ++calls; return Database::CommitOutcome::Committed;
        }, error));
        db.markDirty();
    }
    CHECK_EQ(calls, 1);
}

TEST(shard_database_corrupt_snapshot_never_becomes_empty_local_database) {
    Database db; std::string error; int calls = 0;
    CHECK(!db.loadAuthoritative("{\"players\":[]}", [&](const std::string&) {
        ++calls; return Database::CommitOutcome::Committed;
    }, error));
    CHECK(db.loadFailed()); CHECK(!db.save()); CHECK_EQ(calls, 0);
    CHECK(!db.load("must-never-be-created.json", error));
}
