#include "test.h"
#include "server/shard_authority.h"
#include "server/shard_store.h"
#include <atomic>
#include <thread>
#include <vector>
using namespace flix;
using namespace flix::shards;
namespace {
Node one{"one","boot1"},two{"two","boot2"};
Json records(){Json r=Json::object();r["alice"]=Json::object();r["alice"]["xp"]=25;r["alice"]["bag"]=7;r["bob"]=Json::object();r["bob"]["bag"]=3;return r;}
Json req(const char* op,const char* id,const char* account="alice"){Json r=Json::object();r["op"]=op;r["requestId"]=id;r["account"]=account;return r;}
Json writeReq(const char* op,const char* id,const Json& lease,int bag=8,const char* account="alice"){
 auto r=req(op,id,account);r["epoch"]=lease["epoch"];r["revision"]=lease["revision"];r["record"]=records()[account];r["record"]["bag"]=bag;return r;
}
struct Fixture {
 Json disk=Authority::initial(records());Durability mode=Durability::Committed;int commits=0;
 Authority authority{disk,[this](const Json& next){++commits;if(mode!=Durability::NotCommitted)disk=next;return mode;}};
};
}
TEST(shard_roundtrip_fences_source_and_preserves_final_record) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));CHECK(a["ok"].asBool());
 CHECK(!f.authority.apply(two,req("acquire","duplicate"))["ok"].asBool());
 auto prepare=writeReq("prepare","move",a,99);prepare["target"]="two";prepare["transfer"]="t1";
 auto saved=f.authority.apply(one,prepare);CHECK(saved["ok"].asBool());CHECK_EQ(f.disk["accounts"]["alice"]["record"]["bag"].asInt(),99);
 CHECK(!f.authority.apply(one,writeReq("checkpoint","stale",a,999))["ok"].asBool());
 auto claim=req("claim","arrive");claim["transfer"]="t1";
 CHECK(!f.authority.apply(Node{"other","boot3"},claim)["ok"].asBool());
 auto b=f.authority.apply(two,claim);CHECK(b["ok"].asBool());CHECK_EQ(b["record"]["bag"].asInt(),99);
 auto cancel=req("cancel","cancel");cancel["transfer"]="t1";CHECK(!f.authority.apply(one,cancel)["ok"].asBool());
 auto back=writeReq("prepare","return",b,100);back["target"]="one";back["transfer"]="t2";
 CHECK(f.authority.apply(two,back)["ok"].asBool());claim["requestId"]="returned";claim["transfer"]="t2";
 auto c=f.authority.apply(one,claim);CHECK(c["ok"].asBool());CHECK_EQ(c["record"]["bag"].asInt(),100);
 CHECK(c["epoch"].asDouble()>b["epoch"].asDouble());
}
TEST(shard_lost_response_replay_and_conflicting_request_id) {
 Fixture f;const auto login=req("acquire","login");auto a=f.authority.apply(one,login);
 CHECK(f.authority.apply(one,login)["replayed"].asBool());CHECK_EQ(f.commits,1);
 auto changed=login;changed["account"]="bob";CHECK_EQ(f.authority.apply(one,changed)["error"].asString(),"request_id_reused");
 auto p=writeReq("prepare","prepare",a);p["target"]="two";p["transfer"]="lost-response";
 CHECK(f.authority.apply(one,p)["ok"].asBool());
 Authority restarted(f.disk,[&](const Json& next){f.disk=next;return Durability::Committed;});
 CHECK(restarted.apply(one,p)["replayed"].asBool());
 auto claim=req("claim","claim");claim["transfer"]="lost-response";
 CHECK(restarted.apply(two,claim)["ok"].asBool());CHECK(restarted.apply(two,claim)["replayed"].asBool());
 CHECK(!restarted.apply(one,login)["currentOwnership"].asBool());
}
TEST(shard_failed_prepare_retains_source_and_retry_is_safe) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));const auto before=f.disk.dump();
 auto p=writeReq("prepare","prepare",a);p["target"]="two";p["transfer"]="t1";
 f.mode=Durability::NotCommitted;CHECK_EQ(f.authority.apply(one,p)["error"].asString(),"save_failed");
 CHECK_EQ(f.disk.dump(),before);CHECK_EQ(f.authority.snapshot().dump(),before);
 auto claim=req("claim","claim");claim["transfer"]="t1";CHECK(!f.authority.apply(two,claim)["ok"].asBool());
 f.mode=Durability::Committed;CHECK(f.authority.apply(one,p)["ok"].asBool());CHECK(f.authority.apply(two,claim)["ok"].asBool());
}
TEST(shard_ambiguous_commit_freezes_until_disk_recovery) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));
 auto p=writeReq("prepare","prepare",a);p["target"]="two";p["transfer"]="t1";
 f.mode=Durability::Unknown;CHECK_EQ(f.authority.apply(one,p)["error"].asString(),"commit_unknown");CHECK(!f.authority.healthy());
 CHECK_EQ(f.authority.apply(one,p)["error"].asString(),"authority_frozen");
 Authority recovered(f.disk,[&](const Json& n){f.disk=n;return Durability::Committed;});CHECK(recovered.healthy());
 CHECK(recovered.apply(one,p)["replayed"].asBool());
 auto claim=req("claim","claim");claim["transfer"]="t1";CHECK(recovered.apply(two,claim)["ok"].asBool());
}
TEST(shard_cancel_bumps_epoch_and_transfer_id_cannot_be_reused) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));
 auto p=writeReq("prepare","prepare",a);p["target"]="two";p["transfer"]="t1";CHECK(f.authority.apply(one,p)["ok"].asBool());
 auto cancel=req("cancel","cancel");cancel["transfer"]="t1";
 auto fresh=f.authority.apply(one,cancel);CHECK(fresh["ok"].asBool());
 CHECK(!f.authority.apply(one,writeReq("checkpoint","old",a))["ok"].asBool());
 p=writeReq("prepare","again",fresh);p["target"]="two";p["transfer"]="t1";
 CHECK_EQ(f.authority.apply(one,p)["error"].asString(),"transfer_reused");
 auto claim=req("claim","late");claim["transfer"]="t1";CHECK(!f.authority.apply(two,claim)["ok"].asBool());
 CHECK(f.authority.apply(one,writeReq("checkpoint","new",fresh))["ok"].asBool());
}
TEST(shard_concurrent_login_has_exactly_one_owner) {
 Fixture f;std::atomic<int> winners{0};std::vector<std::thread> threads;
 for(int i=0;i<16;++i)threads.emplace_back([&,i]{Node n{"shard"+std::to_string(i),"boot"};auto r=f.authority.apply(n,req("acquire","login"));if(r["ok"].asBool())++winners;});
 for(auto& t:threads)t.join();CHECK_EQ(winners.load(),1);CHECK_EQ(f.commits,1);
}
TEST(shard_process_restart_does_not_steal_live_lease) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));
 CHECK(!f.authority.apply(Node{"one","newBoot"},req("acquire","login"))["ok"].asBool());
 CHECK(!f.authority.apply(Node{"one","newBoot"},writeReq("checkpoint","checkpoint",a))["ok"].asBool());
 // No timeout-based takeover: operator recovery is intentionally not exposed here.
 CHECK_EQ(f.authority.snapshot()["accounts"]["alice"]["boot"].asString(),"boot1");
}
TEST(shard_global_transaction_requires_all_h_owners_and_matching_revision) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","alice"));auto b=f.authority.apply(two,req("acquire","bob","bob"));
 Json globals=Json::object();globals["hyperForges"]["titan.basic"]["owner"]="bob";
 auto tx=req("global_commit","bad");tx["globalRevision"]=0;tx["globals"]=globals;tx["writes"]=Json::array();
 auto aw=writeReq("checkpoint","ignored",a,8);tx["writes"].push(aw);
 CHECK_EQ(f.authority.apply(one,tx)["error"].asString(),"hyper_owner_not_locked");
 auto bw=writeReq("checkpoint","ignored",b,4,"bob");tx["writes"].push(bw);
 CHECK_EQ(f.authority.apply(one,tx)["error"].asString(),"account_conflict");
 auto prep=writeReq("prepare","freeze-bob",b,4,"bob");prep["target"]="one";prep["transfer"]="global-tx";CHECK(f.authority.apply(two,prep)["ok"].asBool());
 auto claim=req("claim","take-bob","bob");claim["transfer"]="global-tx";b=f.authority.apply(one,claim);CHECK(b["ok"].asBool());
 tx["writes"][1]=writeReq("checkpoint","ignored",b,4,"bob");
 const auto before=f.disk.dump();f.mode=Durability::NotCommitted;CHECK_EQ(f.authority.apply(one,tx)["error"].asString(),"save_failed");CHECK_EQ(f.disk.dump(),before);
 f.mode=Durability::Committed;CHECK(f.authority.apply(one,tx)["ok"].asBool());
 CHECK_EQ(f.disk["globals"]["hyperForges"]["titan.basic"]["owner"].asString(),"bob");
 CHECK_EQ(f.disk["accounts"]["alice"]["record"]["bag"].asInt(),8);CHECK_EQ(f.disk["accounts"]["bob"]["record"]["bag"].asInt(),4);
 tx["requestId"]="stale-global";CHECK_EQ(f.authority.apply(one,tx)["error"].asString(),"global_conflict");
 CHECK(!f.authority.apply(two,writeReq("checkpoint","stale-bob",b,100,"bob"))["ok"].asBool());
}
TEST(shard_corrupt_state_is_never_replaced_by_empty_database) {
 Json malformed=Authority::initial(records());malformed["accounts"]["alice"]["phase"]="bad";int writes=0;
 Authority a(malformed,[&](const Json&){++writes;return Durability::Committed;});CHECK(!a.healthy());
 CHECK_EQ(a.apply(one,req("acquire","login"))["error"].asString(),"authority_frozen");CHECK_EQ(writes,0);
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
TEST(shard_native_store_exclusive_lock_and_durable_recovery) {
 char temporary[]="/tmp/flix-shards-XXXXXX";const auto directory=::mkdtemp(temporary);CHECK(directory!=nullptr);if(!directory)return;
 const std::string path=std::string(directory)+"/authority.json";
 {
  FileStore store(path);CHECK(store.owned());FileStore duplicate(path);CHECK(!duplicate.owned());
  CHECK(store.commit(Authority::initial(records()))==Durability::Committed);
  Json initial;std::string error;CHECK(store.load(initial,error));Authority authority(initial,[&](const Json& state){return store.commit(state);});
  auto lease=authority.apply(one,req("acquire","login"));auto p=writeReq("prepare","prepare",lease,123);p["target"]="two";p["transfer"]="durable";
  CHECK(authority.apply(one,p)["ok"].asBool());
 }
 {
  FileStore store(path);CHECK(store.owned());Json initial;std::string error;CHECK(store.load(initial,error));
  Authority authority(initial,[&](const Json& state){return store.commit(state);});auto c=req("claim","claim");c["transfer"]="durable";
  auto result=authority.apply(two,c);CHECK(result["ok"].asBool());CHECK_EQ(result["record"]["bag"].asInt(),123);
 }
 ::unlink(path.c_str());::unlink((path+".lock").c_str());::rmdir(directory);
}
#endif

TEST(shard_supervised_stopped_process_recovery_fences_old_boot) {
 Fixture f;auto a=f.authority.apply(one,req("acquire","login"));
 auto checkpoint=writeReq("checkpoint","checkpoint",a,200);auto durable=f.authority.apply(one,checkpoint);CHECK(durable["ok"].asBool());
 auto recover=req("recover_stopped","recovery");recover["epoch"]=durable["epoch"];recover["stoppedShard"]="one";recover["stoppedBoot"]="boot1";
 CHECK_EQ(f.authority.apply(one,recover)["error"].asString(),"supervisor_required");
 CHECK(f.authority.apply(Node{"control","controlBoot",Role::Supervisor},recover)["ok"].asBool());
 auto fresh=f.authority.apply(Node{"one","newBoot"},req("acquire","login"));CHECK(fresh["ok"].asBool());CHECK_EQ(fresh["record"]["bag"].asInt(),200);
 CHECK(!f.authority.apply(one,writeReq("checkpoint","oldBoot",durable,999))["ok"].asBool());
 CHECK(!f.authority.apply(Node{"control","controlBoot",Role::Supervisor},req("acquire","duplicate"))["ok"].asBool());
}
