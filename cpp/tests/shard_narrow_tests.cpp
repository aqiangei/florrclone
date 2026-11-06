#include "test.h"
#include "server/shard_authority.h"
using namespace flix;
using namespace flix::shards;
namespace {
Json narrowSeed() {
    Json records=Json::object();records["a"]["totalXP"]=99;
    Json globals=Json::object();globals["users"]["Alice"]["id"]="a";
    globals["users"]["Alice"]["username"]="Alice";globals["sessions"]["token"]["userId"]="a";
    globals["legacyFlorrcloneRoot"]=std::string(4*1024*1024,'x');
    return Authority::initial(records,globals);
}
Json numbered(const char* op,int n) {Json r=Json::object();r["op"]=op;r["requestId"]="r"+std::to_string(n);r["serial"]=n;r["ackThrough"]=0;return r;}
}
TEST(shard_narrow_reads_and_reaffirm_keep_identity_fences) {
    auto state=narrowSeed();Json delta;
    Authority authority(state,Authority::DeltaJournal{},[&](const Json& value){delta=value;return Durability::Committed;});
    Node node{"one","boot"};auto query=Json::parseOrNull(R"({"op":"read_globals","keys":{"users":["Alice","Missing"],"sessions":[],"globals":["councilPolls"]}})");
    const auto read=authority.apply(node,query);CHECK(read["ok"].asBool());CHECK(read.dump().size()<500);
    CHECK(read["patch"]["users"]["Missing"].isNull());CHECK(delta.isNull());
    auto acquire=numbered("acquire",1);acquire["account"]="a";
    const auto lease=authority.apply(node,acquire);CHECK(lease["ok"].asBool());
    auto again=numbered("reaffirm",2);again["account"]="a";again["epoch"]=lease["epoch"];again["revision"]=lease["revision"];
    again["identityKey"]="Alice";again["expectedIdentity"]=state["globals"]["users"]["Alice"];
    again["sessionHash"]="token";again["expectedSession"]=state["globals"]["sessions"]["token"];
    CHECK(authority.apply(node,again)["ok"].asBool());
    CHECK_EQ(authority.snapshot()["accounts"]["a"]["epoch"].asInt(),lease["epoch"].asInt());
    again["serial"]=3;again["requestId"]="r3";again["expectedIdentity"]["admin"]=true;
    CHECK_EQ(authority.apply(node,again)["error"].asString(),"identity_changed");
    auto owned=authority.apply(node,Json::parseOrNull(R"({"op":"read_owned"})"));CHECK_EQ(owned["accounts"].size(),1u);
    CHECK_EQ(authority.apply({"one","other-boot"},Json::parseOrNull(R"({"op":"read_owned"})"))["accounts"].size(),0u);
}
TEST(shard_planned_patch_deduplicates_original_command_without_copying_history) {
    Json delta;auto state=narrowSeed();int plans=0;
    Authority authority(state,Authority::DeltaJournal{},[&](const Json& value){delta=value;return Durability::Committed;});
    const auto request=numbered("council",1);
    const auto planner=[&](const Json& current){
        ++plans;Json result=Json::object();result["op"]="global_patch";result["globalRevision"]=current["globalRevision"];
        result["writes"]=Json::array();result["patch"]["globals"]["councilPolls"]["id"]["title"]="one";return result;
    };
    CHECK(authority.apply({"one","boot"},request,planner)["ok"].asBool());CHECK(delta.dump().size()<2000);
    CHECK(Authority::recoverDelta(state,delta));CHECK_EQ(state["globals"]["councilPolls"]["id"]["title"].asString(),"one");
    CHECK_EQ(state["globals"]["legacyFlorrcloneRoot"].asString().size(),4u*1024u*1024u);
    CHECK(authority.apply({"one","boot"},request,planner)["replayed"].asBool());CHECK_EQ(plans,1);
}
