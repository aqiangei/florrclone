#include "test.h"
#include "server/shard_journal.h"
#include <fstream>
using namespace flix;
using namespace flix::shards;
namespace {
Json initialState(int count=1) {
    Json records=Json::object();
    for(int i=0;i<count;++i){Json record=Json::object();record["xp"]=i;records["a"+std::to_string(i)]=record;}
    return Authority::initial(records);
}
Json acquire(const char* id="login") {Json r=Json::object();r["op"]="acquire";r["account"]="a0";r["requestId"]=id;return r;}
}
TEST(shard_delta_does_not_copy_or_persist_unrelated_accounts) {
    auto state=initialState(3500);Json delta;
    Authority authority(state,Authority::DeltaJournal{},[&](const Json& next){delta=next;return Durability::Committed;});
    auto reply=authority.apply({"one","boot"},acquire());
    CHECK(reply["ok"].asBool());CHECK_EQ(delta["accounts"].size(),1u);
    CHECK(delta.dump().size()<1500);
    CHECK(Authority::recoverDelta(state,delta));
    CHECK_EQ(state["accounts"]["a0"]["phase"].asString(),"active");
    CHECK_EQ(state["accounts"]["a3499"]["phase"].asString(),"offline");
    const auto saved=state.dump();CHECK(!Authority::recoverDelta(state,delta));CHECK_EQ(state.dump(),saved);
    CHECK(authority.apply({"one","boot"},acquire())["replayed"].asBool());
}
TEST(shard_delta_rejection_and_unknown_keep_fences_closed) {
    int calls=0;auto outcome=Durability::NotCommitted;
    Authority authority(initialState(),Authority::DeltaJournal{},[&](const Json&){++calls;return outcome;});
    CHECK(!authority.apply({"one","boot"},acquire())["ok"].asBool());
    CHECK_EQ(authority.snapshot()["accounts"]["a0"]["phase"].asString(),"offline");
    outcome=Durability::Unknown;
    CHECK(!authority.apply({"one","boot"},acquire())["ok"].asBool());CHECK(!authority.healthy());
    CHECK(!authority.apply({"two","boot"},acquire("retry"))["ok"].asBool());CHECK_EQ(calls,2);
}
TEST(shard_delta_registration_atomically_creates_identity_record_and_owner) {
    Json globals=Json::object();globals["users"]=Json::object();
    auto disk=Authority::initial(Json::object(),globals);
    Authority authority(disk,Authority::DeltaJournal{},[&](const Json& delta){
        CHECK(Authority::recoverDelta(disk,delta));return Durability::Committed;
    });
    Json req=Json::object();req["op"]="global_commit";req["requestId"]="register";req["globalRevision"]=0;
    req["globals"]=globals;req["globals"]["users"]["Alice"]["id"]="new-account";
    req["writes"]=Json::array();Json write=Json::object();write["account"]="new-account";
    write["record"]["totalXP"]=4;write["create"]=true;write["username"]="Alice";req["writes"].push(write);
    auto result=authority.apply({"one","boot"},req);CHECK(result["ok"].asBool());
    CHECK_EQ(disk["accounts"]["new-account"]["phase"].asString(),"active");
    CHECK_EQ(disk["accounts"]["new-account"]["record"]["totalXP"].asInt(),4);
    CHECK_EQ(disk["globals"]["users"]["Alice"]["id"].asString(),"new-account");
    CHECK(authority.apply({"one","boot"},req)["replayed"].asBool());
    req["requestId"]="duplicate";req["globalRevision"]=1;
    req["globals"]["users"]["alice"]["id"]="another-account";
    req["writes"][0]["account"]="another-account";req["writes"][0]["username"]="alice";
    CHECK_EQ(authority.apply({"two","boot"},req)["error"].asString(),"account_creation_conflict");
}
TEST(shard_journal_recovers_fence_and_discards_only_incomplete_tail) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    char directory[]="/tmp/shards-journal-test-XXXXXX";CHECK(::mkdtemp(directory)!=nullptr);
    const std::string base=std::string(directory)+"/authority";std::string error;
    {
        JournalStore journal(base);Json recovered;
        CHECK(journal.open(initialState(),recovered,error));
        Authority authority(recovered,Authority::DeltaJournal{},[&](const Json& d){return journal.append(d);});
        CHECK(authority.apply({"one","boot"},acquire())["ok"].asBool());
        Json world;
        world["op"]="checkpoint_batch";world["requestId"]="world";world["writes"]=Json::array();
        world["world"]["bossClocks"]["garden"]["uniqueReadyAt"]=123;
        CHECK(authority.apply({"one","boot"},world)["ok"].asBool());
    }
    {std::ofstream out(base+".journal",std::ios::app);out<<"{incomplete";}
    {
        JournalStore journal(base);Json recovered;CHECK(journal.open(initialState(),recovered,error));
        CHECK_EQ(recovered["globals"]["shardWorlds"]["one"]["bossClocks"]["garden"]["uniqueReadyAt"].asInt(),123);
        Authority authority(recovered,Authority::DeltaJournal{},[&](const Json& d){return journal.append(d);});
        CHECK(authority.apply({"one","boot"},acquire())["replayed"].asBool());
        CHECK(!authority.apply({"two","other"},acquire("steal"))["ok"].asBool());
    }
    {std::ofstream out(base+".journal",std::ios::app);out<<"{corrupt}\n";}
    {JournalStore journal(base);Json recovered;CHECK(!journal.open(initialState(),recovered,error));}
    std::remove((base+".snapshot").c_str());std::remove((base+".snapshot.lock").c_str());
    std::remove((base+".journal").c_str());::rmdir(directory);
#endif
}
