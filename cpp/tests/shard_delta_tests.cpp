#include "test.h"
#include "server/shard_authority.h"
using namespace flix;using namespace flix::shards;
namespace {
const Node one{"one","delta-one"},two{"two","delta-two"};
Json initial(){Json records;records["alice"]["xp"]=10;records["bob"]["xp"]=20;
 Json globals;globals["users"]["Alice"]["id"]="alice";globals["users"]["Bob"]["id"]="bob";
 globals["sessions"]["token-hash"]["userId"]="alice";globals["legacyFlorrcloneRoot"]["preserved"]=std::string(4*1024*1024,'x');
 globals["hyperForges"]["egg"]["owner"]="alice";return Authority::initial(records,globals);}
Json request(const char* op,int serial){Json r;r["op"]=op;r["requestId"]="r"+std::to_string(serial);r["serial"]=serial;r["ackThrough"]=serial-1;return r;}
Json login(const char* account,int serial){auto r=request("acquire",serial);r["account"]=account;return r;}
Json write(const char* account,const Json& lease,int xp){Json w;w["account"]=account;w["epoch"]=lease["epoch"];w["revision"]=lease["revision"];w["record"]["xp"]=xp;return w;}
struct F {
 Json disk=initial(),last;int commits=0;bool fail=false;
 Authority a{disk,Authority::DeltaJournal{},[&](const Json& delta){if(fail)return Durability::NotCommitted;last=delta;++commits;return Authority::recoverDelta(disk,delta)?Durability::Committed:Durability::Unknown;}};
};
}
TEST(shard_read_identity_returns_only_requested_account_and_does_not_write){
 F f;Json query;query["op"]="read_identity";query["username"]="aLiCe";auto result=f.a.apply(one,query);
 CHECK(result["ok"].asBool());CHECK(result["found"].asBool());CHECK_EQ(result["identityKey"].asString(),"Alice");
 CHECK_EQ(result["account"].asString(),"alice");CHECK_EQ(result["record"]["xp"].asInt(),10);CHECK_EQ(result["ownership"]["phase"].asString(),"offline");
 CHECK(result.dump().size()<1024);CHECK(result["state"].isNull());CHECK_EQ(f.commits,0);
 query.erase("username");query["sessionHash"]="token-hash";result=f.a.apply(one,query);CHECK_EQ(result["session"]["userId"].asString(),"alice");
 query["sessionHash"]="missing";CHECK(!f.a.apply(one,query)["found"].asBool());CHECK_EQ(f.commits,0);
}
TEST(shard_checkpoint_batch_is_atomic_and_omits_unchanged_global_payload){
 F f;auto a=f.a.apply(one,login("alice",1)),b=f.a.apply(two,login("bob",1));
 auto tx=request("checkpoint_batch",2);tx["writes"]=Json::array();tx["writes"].push(write("alice",a,50));tx["writes"].push(write("bob",b,60));
 CHECK_EQ(f.a.apply(one,tx)["error"].asString(),"account_conflict");CHECK_EQ(f.disk["accounts"]["alice"]["record"]["xp"].asInt(),10);
 tx["writes"]=Json::array();tx["writes"].push(write("alice",a,50));auto result=f.a.apply(one,tx);CHECK(result["ok"].asBool());
 CHECK(result["globalRevision"].isNull());CHECK(f.last["globals"].isNull());CHECK(f.last["globalPatch"].isNull());CHECK(f.last.dump().size()<2048);
 CHECK_EQ(f.disk["globalRevision"].asInt(),0);CHECK_EQ(f.disk["accounts"]["alice"]["record"]["xp"].asInt(),50);
 CHECK_EQ(f.disk["globals"]["legacyFlorrcloneRoot"]["preserved"].asString().size(),4u*1024u*1024u);
}
TEST(shard_global_patch_preserves_unrelated_tables_and_replays_incremental_state){
 F f;auto lease=f.a.apply(one,login("alice",1));auto tx=request("global_patch",2);tx["globalRevision"]=0;
 tx["writes"]=Json::array();tx["writes"].push(write("alice",lease,99));
 tx["patch"]["sessions"]["token-hash"]=Json();tx["patch"]["sessions"]["new-token"]["userId"]="alice";
 tx["patch"]["globals"]["councilActivity"]["alice"]=15;
 const auto before=f.disk;auto result=f.a.apply(one,tx);CHECK(result["ok"].asBool());CHECK(f.last["globals"].isNull());CHECK(f.last.dump().size()<2048);
 CHECK_EQ(result["globalRevision"].asInt(),1);CHECK(static_cast<const Json&>(f.disk)["globals"]["sessions"]["token-hash"].isNull());
 CHECK_EQ(f.disk["globals"]["sessions"]["new-token"]["userId"].asString(),"alice");CHECK_EQ(f.disk["globals"]["councilActivity"]["alice"].asInt(),15);
 CHECK_EQ(f.disk["globals"]["legacyFlorrcloneRoot"].dump(),before["globals"]["legacyFlorrcloneRoot"].dump());
 auto replay=before;CHECK(Authority::recoverDelta(replay,f.last));CHECK(canonical(replay).dump()==canonical(f.disk).dump());
 CHECK(f.a.apply(one,tx)["replayed"].asBool());CHECK_EQ(f.disk["sequence"].asInt(),2);
}
TEST(shard_global_patch_requires_h_owners_and_does_not_commit_on_conflict_or_save_failure){
 F f;auto lease=f.a.apply(one,login("alice",1));auto tx=request("global_patch",2);tx["globalRevision"]=0;
 tx["writes"]=Json::array();tx["writes"].push(write("alice",lease,90));tx["patch"]["globals"]["hyperForges"]["egg"]["owner"]="bob";
 CHECK_EQ(f.a.apply(one,tx)["error"].asString(),"hyper_owner_not_locked");CHECK_EQ(f.disk["accounts"]["alice"]["record"]["xp"].asInt(),10);
 tx["patch"]=Json::object();tx["patch"]["sessions"]["new-token"]["userId"]="alice";
 tx["globalRevision"]=999;CHECK_EQ(f.a.apply(one,tx)["error"].asString(),"global_conflict");
 tx["globalRevision"]=0;f.fail=true;CHECK_EQ(f.a.apply(one,tx)["error"].asString(),"save_failed");
 CHECK(f.disk["globals"]["sessions"]["new-token"].isNull());CHECK_EQ(f.disk["sequence"].asInt(),1);
}
TEST(shard_global_patch_registration_checks_full_username_index){
 F f;auto tx=request("global_patch",1);tx["globalRevision"]=0;tx["writes"]=Json::array();
 Json create;create["account"]="new";create["create"]=true;create["username"]="ALICE";create["record"]["xp"]=1;tx["writes"].push(create);
 tx["patch"]["users"]["ALICE"]["id"]="new";CHECK_EQ(f.a.apply(one,tx)["error"].asString(),"account_creation_conflict");
 tx["writes"][0]["username"]="New";tx["patch"]["users"].erase("ALICE");tx["patch"]["users"]["New"]["id"]="new";
 auto result=f.a.apply(one,tx);CHECK(result["ok"].asBool());CHECK_EQ(f.disk["globals"]["users"]["Bob"]["id"].asString(),"bob");
 CHECK_EQ(f.disk["globals"]["users"]["New"]["id"].asString(),"new");CHECK(f.last.dump().size()<2048);
}
