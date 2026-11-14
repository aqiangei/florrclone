#include "test.h"
#include "server/hyper_forge.h"
#include "server/shard_hyper.h"
using namespace flix;
using namespace flix::shards;

namespace {
Json hyperSeed() {
    Json records=Json::object(),globals=Json::object();
    records["a"]["inventory"]["unique"]["petal_rose"]=20;
    records["b"]["inventory"]["apex"]["petal_rose"]=1;
    records["b"]["inventory"]["unique"]["petal_rose"]=2;
    records["a"]["unknownKept"]="winner-extra";records["b"]["unknownKept"]="loser-extra";
    globals["hyperForges"]["rose"]["owner"]="b";
    globals["hyperForges"]["rose"]["claims"]=1;globals["hyperForges"]["rose"]["paid"]=5;
    globals["legacyFlorrcloneRoot"]="retained-history";
    return Authority::initial(records,globals);
}
Json acquireHyper(Authority& authority,const Node& node,const char* id) {
    Json request=Json::object();request["op"]="acquire";request["requestId"]=std::string("take-")+id;request["account"]=id;
    return authority.apply(node,request);
}
Json hyperInput(const Json& a,const Json& b) {
    Json input=Json::object();input["op"]="hyper";input["requestId"]="forge-once";
    input["account"]="a";input["action"]="forge";input["input"]="rose";
    for(const auto& pair:std::vector<std::pair<std::string,Json>>{{"a",a},{"b",b}}) {
        input["leases"][pair.first]["epoch"]=pair.second["epoch"];
        input["leases"][pair.first]["revision"]=pair.second["revision"];
    }
    return input;
}
}

TEST(shard_hyper_planner_commits_real_rules_once_and_replays_after_recovery) {
    auto state=hyperSeed();Json delta;Node node{"one","transaction-actor"};
    Authority authority(state,Authority::DeltaJournal{},[&](const Json& value){delta=value;return Durability::Committed;});
    const auto a=acquireHyper(authority,node,"a"),b=acquireHyper(authority,node,"b");
    CHECK(a["ok"].asBool()&&b["ok"].asBool());const auto input=hyperInput(a,b);
    Json business;int plans=0;
    auto plan=[&](const Json& current){++plans;return planHeldHyper(current,node,input,1000,business);};
    const auto committed=authority.apply(node,input,plan);CHECK(committed["ok"].asBool());CHECK(business["ok"].asBool());
    CHECK_EQ(business["consumed"].asInt(),5);CHECK_EQ(delta["accounts"].size(),2u);
    CHECK(delta["globals"].isNull());CHECK(delta["globalPatch"]["globals"]["legacyFlorrcloneRoot"].isNull());
    const auto after=authority.snapshot();
    CHECK_EQ(after["accounts"]["a"]["record"]["inventory"]["unique"]["petal_rose"].asInt(),15);
    CHECK_EQ(after["accounts"]["a"]["record"]["inventory"]["apex"]["petal_rose"].asInt(),1);
    CHECK_EQ(after["accounts"]["b"]["record"]["inventory"]["apex"]["petal_rose"].asInt(),0);
    CHECK_EQ(after["accounts"]["b"]["record"]["inventory"]["unique"]["petal_rose"].asInt(),6);
    CHECK(!after["accounts"]["b"]["record"]["hyperHistory"]["rose"].isNull());
    CHECK_EQ(after["accounts"]["b"]["record"]["unknownKept"].asString(),"loser-extra");
    CHECK_EQ(after["globals"]["hyperForges"]["rose"]["owner"].asString(),"a");
    CHECK_EQ(after["globals"]["legacyFlorrcloneRoot"].asString(),"retained-history");
    Authority recovered(after,[](const Json&){return Durability::Committed;});
    CHECK(recovered.apply(node,input,plan)["replayed"].asBool());CHECK_EQ(plans,1);
    CHECK_EQ(recovered.snapshot().dump(),after.dump());
}

TEST(shard_apex_hammer_transfer_is_atomic_and_replays_once) {
    Json records = Json::object(), globals = Json::object();
    const auto key = hyperForgeKey("mjolnir", "garden", true);
    records["a"]["inventory"]["ascendant"]["petal_fragment"] = 5;
    records["b"]["inventory"]["ascendant"]["petal_mjolnir"] = 1;
    records["b"]["inventory"]["apex"]["petal_mjolnir"] = 1;
    globals["hyperForges"][key]["owner"] = "b";
    globals["hyperForges"][key]["paid"] = 5;
    globals["hyperForges"][key]["input"] = "fragment";
    globals["hyperForges"]["garden:mjolnir"]["owner"] = "b";
    const Node node{"one", "transaction-actor"};
    Authority authority(Authority::initial(records, globals),
        [](const Json&) { return Durability::Committed; });
    const auto a = acquireHyper(authority, node, "a");
    const auto b = acquireHyper(authority, node, "b");
    auto input = hyperInput(a, b);
    input["input"] = "fragment";
    input["titan"] = "garden";
    input["apexHammer"] = true;
    Json business;
    const auto plan = [&](const Json& state) { return planHeldHyper(state, node, input, 1000, business); };
    CHECK(authority.apply(node, input, plan)["ok"].asBool());
    CHECK(business["ok"].asBool());
    CHECK_EQ(business["consumed"].asInt(), 5);
    const auto snapshot = authority.snapshot();
    CHECK_EQ(snapshot["globals"]["hyperForges"][key]["owner"].asString(), "a");
    CHECK_EQ(snapshot["globals"]["hyperForges"]["garden:mjolnir"]["owner"].asString(), "b");
    CHECK_EQ(snapshot["accounts"]["a"]["record"]["inventory"]["ascendant"]["petal_mjolnir"].asInt(), 1);
    CHECK_EQ(snapshot["accounts"]["b"]["record"]["inventory"]["ascendant"]["petal_fragment"].asInt(), 4);
    CHECK_EQ(snapshot["accounts"]["b"]["record"]["inventory"]["apex"]["petal_mjolnir"].asInt(), 1);
    CHECK(authority.apply(node, input, plan)["replayed"].asBool());
    CHECK_EQ(authority.snapshot().dump(), snapshot.dump());
}
TEST(shard_hyper_planner_rejects_foreign_or_stale_owner_before_business_changes) {
    Node one{"one","boot"},two{"two","boot"};int saves=0;
    Authority authority(hyperSeed(),[&](const Json&){++saves;return Durability::Committed;});
    const auto a=acquireHyper(authority,one,"a"),b=acquireHyper(authority,two,"b");
    const auto input=hyperInput(a,b),before=authority.snapshot();Json business;
    auto result=authority.apply(one,input,[&](const Json& state){return planHeldHyper(state,one,input,1000,business);});
    CHECK(!result["ok"].asBool());CHECK_EQ(business["error"].asString(),"hyper_participant_not_held");
    CHECK_EQ(saves,2);CHECK_EQ(authority.snapshot().dump(),before.dump());
}

TEST(shard_hyper_planner_failed_fsync_leaves_both_players_and_global_owner_unchanged) {
    Node node{"one","boot"};bool reject=false;
    Authority authority(hyperSeed(),[&](const Json&){return reject?Durability::NotCommitted:Durability::Committed;});
    const auto a=acquireHyper(authority,node,"a"),b=acquireHyper(authority,node,"b");
    const auto input=hyperInput(a,b),before=authority.snapshot();Json business;reject=true;
    auto result=authority.apply(node,input,[&](const Json& state){return planHeldHyper(state,node,input,1000,business);});
    CHECK_EQ(result["error"].asString(),"save_failed");CHECK_EQ(authority.snapshot().dump(),before.dump());
}

TEST(shard_hyper_legacy_mutations_cannot_submit_then_rollback_pending_commit) {
    Json seed=Json::object();seed["users"]=Json::object();
    seed["players"]["a"]["inventory"]["unique"]["petal_rose"]=20;
    seed["players"]["b"]["inventory"]["apex"]["petal_rose"]=1;
    seed["hyperForges"]["rose"]["owner"]="b";
    seed["hyperForges"]["rose"]["claims"]=1;
    seed["hyperForges"]["rose"]["paid"]=5;
    Database db;std::string error;int submitted=0;
    CHECK(db.loadAuthoritative(seed.dump(),[](const std::string&){return Database::CommitOutcome::Committed;},error));
    CHECK(db.setDeltaSink([&](const Json&){++submitted;return Database::CommitOutcome::Pending;}));
    const auto generation=db.mutationGeneration();
    const auto beforeA=*db.findProgress("a"),beforeB=*db.findProgress("b");
    const auto beforeTable=db.storedTable("hyperForges").dump();
    CHECK(!forgeHyper(db,"a","rose",1000).ok);
    CHECK(!abandonHyper(db,"b","rose").ok);
    CHECK(!grantTemporaryHyperHammer(db,"a",1000,"operator-test").ok);
    CHECK_EQ(submitted,0);CHECK(!db.commitPending());CHECK(!db.dirty());
    CHECK_EQ(db.mutationGeneration(),generation);
    CHECK_EQ(db.findProgress("a")->inventory.dump(),beforeA.inventory.dump());
    CHECK_EQ(db.findProgress("b")->inventory.dump(),beforeB.inventory.dump());
    CHECK_EQ(db.storedTable("hyperForges").dump(),beforeTable);
}
