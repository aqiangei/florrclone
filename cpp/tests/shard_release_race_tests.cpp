#include "test.h"
#include "server_harness.h"
using namespace flix; using namespace flix::testsupport;
TEST(shard_release_waits_for_mutations_after_pending_save) {
 bool acknowledge=false; int releases=0; Json committed;
 Harness h("release-race",{},dataDir(),0,[&](ServerConfig& c){
  c.authoritySnapshot="{}";
  c.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
  c.authorityDeltaCommit=[&](const Json& d){if(!acknowledge)return Database::CommitOutcome::Pending; committed=d;return Database::CommitOutcome::Committed;};
  c.authorityReleaseAsync=[&](const std::vector<std::string>&){++releases;Json r=Json::object();r["ok"]=true;return r;};
 });
 CHECK(h.ready);if(!h.ready)return;
 auto& db=GameServerPeer::authorityDb(h.server);
 db.progress("departing").stars=10;db.markDirty();
 GameServerPeer::releaseAuthority(h.server);CHECK(db.commitPending());CHECK_EQ(releases,0);
 db.progress("departing").stars=20;db.markDirty();
 acknowledge=true;GameServerPeer::releaseAuthority(h.server);
 CHECK_EQ(releases,1);CHECK(!db.dirty());
 CHECK_EQ(committed["players"]["departing"]["stars"].asDouble(),20);
 h.server.stop();for(int i=0;i<10&&h.server.step();++i){}h.server.shutdown();
}
