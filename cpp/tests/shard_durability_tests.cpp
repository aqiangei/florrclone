#include "test.h"
#include "server/shard_journal.h"
#include <sys/wait.h>
#include <fstream>
using namespace flix;using namespace flix::shards;
namespace {
Node actor{"one","numbered-boot"};
Json seed(){Json r=Json::object();r["alice"]["xp"]=1;r["bob"]["xp"]=2;return Authority::initial(r);}
Json op(const char* action,int serial,int ack=0,const char* account="alice"){
 Json r=Json::object();r["op"]=action;r["requestId"]="r"+std::to_string(serial);r["serial"]=serial;r["ackThrough"]=ack;r["account"]=account;return r;
}
Json save(const Json& lease,int serial,int ack){auto r=op("checkpoint",serial,ack);r["epoch"]=lease["epoch"];r["revision"]=lease["revision"];r["record"]["xp"]=serial;return r;}
struct Temp {
 std::string directory,base;
 Temp(){char temp[]="/tmp/florr-durability-XXXXXX";auto d=::mkdtemp(temp);if(d){directory=d;base=directory+"/authority";}}
 ~Temp(){if(!directory.empty()){for(const char* s:{".snapshot",".snapshot.tmp",".snapshot.lock",".journal"})::unlink((base+s).c_str());::rmdir(directory.c_str());}}
};
}
TEST(shard_acknowledged_requests_remain_rejected_after_restart_and_account_release){
 Json disk=seed();Authority a(disk,[&](const Json& s){disk=s;return Durability::Committed;});
 auto login=op("acquire",1);auto lease=a.apply(actor,login);CHECK(lease["ok"].asBool());
 for(int n=2;n<=6000;++n){lease=a.apply(actor,save(lease,n,n-1));CHECK(lease["ok"].asBool());}
 CHECK_EQ(disk["requests"].size(),1u);CHECK_EQ(disk["receipts"]["game/one/numbered-boot"].asInt(),5999);
 auto release=save(lease,6001,6000);release["op"]="release";CHECK(a.apply(actor,release)["ok"].asBool());
 Authority restarted(disk,[&](const Json& s){disk=s;return Durability::Committed;});
 CHECK_EQ(restarted.apply(actor,login)["error"].asString(),"request_acknowledged");
 CHECK_EQ(disk["accounts"]["alice"]["phase"].asString(),"offline");
 CHECK_EQ(disk["accounts"]["alice"]["record"]["xp"].asInt(),6001);
}
TEST(shard_unacknowledged_reply_replays_but_serial_reuse_and_self_ack_fail){
 Json disk=seed();int commits=0;Authority a(disk,[&](const Json& s){disk=s;++commits;return Durability::Committed;});
 const auto login=op("acquire",1);auto lease=a.apply(actor,login);auto next=save(lease,2,0);
 CHECK(a.apply(actor,next)["ok"].asBool());CHECK(a.apply(actor,login)["replayed"].asBool());CHECK_EQ(commits,2);
 auto reused=login;reused["requestId"]="different";CHECK_EQ(a.apply(actor,reused)["error"].asString(),"request_id_reused");
 auto self=op("acquire",3,3,"bob");CHECK_EQ(a.apply(actor,self)["error"].asString(),"bad_acknowledgement");
 CHECK_EQ(disk["requests"].size(),2u);
}
TEST(shard_receipt_scope_cannot_acknowledge_another_actor_or_supervisor){
 Json disk=seed();Authority a(disk,[&](const Json& s){disk=s;return Durability::Committed;});
 auto login=op("acquire",1);auto lease=a.apply(actor,login);
 auto other=op("acquire",201,200,"bob");CHECK(a.apply(Node{"two","b"},other)["ok"].asBool());
 CHECK(a.apply(actor,login)["replayed"].asBool());
 auto recover=op("recover_stopped",3,2);recover["epoch"]=lease["epoch"];recover["stoppedShard"]="one";recover["stoppedBoot"]="numbered-boot";
 CHECK_EQ(a.apply(actor,recover)["error"].asString(),"supervisor_required");
 CHECK(a.apply(Node{"one","numbered-boot",Role::Supervisor},recover)["ok"].asBool());
 CHECK(a.apply(actor,login)["replayed"].asBool());
 CHECK(!a.apply(actor,login)["currentOwnership"].asBool());
}
TEST(shard_failed_persist_does_not_advance_receipt_or_forget_replay){
 Json disk=seed();bool fail=false;Authority a(disk,[&](const Json& s){if(fail)return Durability::NotCommitted;disk=s;return Durability::Committed;});
 const auto login=op("acquire",1);auto lease=a.apply(actor,login);fail=true;
 CHECK_EQ(a.apply(actor,save(lease,2,1))["error"].asString(),"save_failed");
 CHECK(a.apply(actor,login)["replayed"].asBool());CHECK_EQ(disk["requests"].size(),1u);
}
TEST(shard_compaction_survives_process_death_at_each_durable_boundary){
 for(const std::string stage:{"before_snapshot","after_snapshot","after_truncate"}){
  Temp temp;CHECK(!temp.base.empty());Json expected;
  {
   JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
   Authority a(loaded,Authority::DeltaJournal{},[&](const Json& delta){return store.append(delta);});
   auto lease=a.apply(actor,op("acquire",1));for(int n=2;n<=20;++n){lease=a.apply(actor,save(lease,n,n-1));CHECK(lease["ok"].asBool());}
   expected=a.snapshot();CHECK_EQ(expected["requests"].size(),1u);
  }
  const pid_t child=::fork();CHECK(child>=0);
  if(child==0){JournalStore store(temp.base);Json loaded;std::string error;if(!store.open(seed(),loaded,error))::_exit(91);
   store.checkpoint(loaded,[&](const char* point){if(stage==point)::_exit(42);});::_exit(92);}
  int status=0;::waitpid(child,&status,0);CHECK(WIFEXITED(status));CHECK_EQ(WEXITSTATUS(status),42);
  {
   JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
   CHECK_EQ(canonical(loaded).dump(),canonical(expected).dump());
   Authority a(loaded,Authority::DeltaJournal{},[&](const Json& delta){return store.append(delta);});
   CHECK_EQ(a.apply(actor,op("acquire",1))["error"].asString(),"request_acknowledged");
   CHECK(a.apply(actor,save(loaded["accounts"]["alice"],21,20))["ok"].asBool());
   CHECK(store.checkpoint(a.snapshot())==Durability::Committed);
   struct stat stat{};CHECK(::stat((temp.base+".journal").c_str(),&stat)==0);CHECK_EQ(stat.st_size,0);
  }
  {
   JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
   CHECK_EQ(loaded["sequence"].asInt(),21);CHECK_EQ(loaded["accounts"]["alice"]["record"]["xp"].asInt(),21);
   CHECK_EQ(loaded["requests"].size(),1u);CHECK_EQ(loaded["receipts"]["game/one/numbered-boot"].asInt(),20);
  }
 }
}
TEST(shard_compaction_rejects_uncommitted_state_and_triggers_on_record_bound){
 Temp temp;JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
 Authority a(loaded,Authority::DeltaJournal{},[&](const Json& delta){return store.append(delta);});
 auto lease=a.apply(actor,op("acquire",1));for(int n=2;n<=2048;++n){lease=a.apply(actor,save(lease,n,n-1));CHECK(lease["ok"].asBool());}
 CHECK(store.compactDue());auto future=a.snapshot();future["sequence"]=3000;
 CHECK(store.checkpoint(future)==Durability::NotCommitted);
 CHECK(store.checkpoint(a.snapshot())==Durability::Committed);CHECK(!store.compactDue());
}
TEST(shard_partial_tail_is_removed_but_complete_corrupt_journal_fails_closed){
 Temp temp;Json expected;
 {
  JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
  Authority a(loaded,Authority::DeltaJournal{},[&](const Json& d){return store.append(d);});
  CHECK(a.apply(actor,op("acquire",1))["ok"].asBool());expected=a.snapshot();
 }
 struct stat before{};CHECK(::stat((temp.base+".journal").c_str(),&before)==0);
 {std::ofstream out(temp.base+".journal",std::ios::app);out<<"{\"payload\":\"incomplete";}
 {
  JournalStore store(temp.base);Json loaded;std::string error;CHECK(store.open(seed(),loaded,error));
  CHECK_EQ(canonical(loaded).dump(),canonical(expected).dump());
  struct stat after{};CHECK(::stat((temp.base+".journal").c_str(),&after)==0);CHECK_EQ(after.st_size,before.st_size);
 }
 {std::ofstream out(temp.base+".journal",std::ios::app);out<<"{\"payload\":\"corrupt\",\"sha256\":\"bad\"}\n";}
 {
  JournalStore store(temp.base);Json loaded;std::string error;CHECK(!store.open(seed(),loaded,error));
  CHECK(error.find("checksum")!=std::string::npos);
 }
}
