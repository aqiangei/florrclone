#include "test.h"
#include "server_harness.h"
using namespace flix;
using namespace flix::testsupport;

TEST(shard_game_stop_waits_for_async_release_after_final_save) {
    bool released=false;int releaseCalls=0;
    Harness h("shard-stop-release",{},dataDir(),0,[&](ServerConfig& cfg){
        cfg.authoritySnapshot="{}";
        cfg.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
        cfg.authorityDeltaCommit=[](const Json&){return Database::CommitOutcome::Committed;};
        cfg.authorityReleaseAsync=[&](const std::vector<std::string>&){
            ++releaseCalls;Json result=Json::object();result[released?"ok":"pending"]=true;return result;
        };
    });
    CHECK(h.ready);if(!h.ready)return;
    h.server.stop();CHECK(h.server.step());CHECK(releaseCalls>0);
    released=true;CHECK(!h.server.step());h.server.shutdown();CHECK_EQ(h.server.exitCode(),0);
}

TEST(shard_game_async_identity_and_grant_wait_without_stopping_simulation) {
    bool lookupReady=false,grantReady=false;int lookups=0,grants=0;
    Harness h("shard-auth-async",{},dataDir(),0,[&](ServerConfig& cfg){
        cfg.authoritySnapshot="{}";
        cfg.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
        cfg.authorityDeltaCommit=[](const Json&){return Database::CommitOutcome::Committed;};
        cfg.authorityAuth=[&](const Json& input){
            Json reply=Json::object();reply["ok"]=true;
            if(input["action"].asString()=="lookup") {
                ++lookups;if(!lookupReady){reply["pending"]=true;return reply;}
                reply["found"]=true;reply["identityKey"]="AsyncLogin";
                reply["identity"]["id"]="async-login";reply["identity"]["username"]="AsyncLogin";
                reply["identity"]["password"]="test-password-123";reply["identity"]["isPlainText"]=true;
            } else if(input["action"].asString()=="acquireAsync") {
                ++grants;if(!grantReady){reply["pending"]=true;return reply;}
                reply["record"]["totalXP"]=99;reply["epoch"]=1;reply["revision"]=0;
            }
            return reply;
        };
    });
    CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(connectClient(h,client));client.requestLogin("AsyncLogin","test-password-123");
    h.step(8,{&client});CHECK(lookups>0);CHECK_EQ(grants,0);CHECK(client.status()!=NetClient::Status::LoggedIn);
    CHECK(h.server.step());lookupReady=true;h.step(8,{&client});CHECK(grants>0);CHECK(client.status()!=NetClient::Status::LoggedIn);
    CHECK(h.server.step());grantReady=true;
    CHECK(h.stepUntil({&client},[&]{return client.status()==NetClient::Status::LoggedIn;}));
    CHECK_EQ(client.profile().totalXp,99);
    h.server.stop();for(int i=0;i<30&&h.server.step();++i){}h.server.shutdown();CHECK_EQ(h.server.exitCode(),0);
}

TEST(shard_game_shutdown_reports_failure_when_release_is_not_confirmed) {
    Harness h("shard-release-failure",{},dataDir(),0,[&](ServerConfig& cfg){
        cfg.authoritySnapshot="{}";
        cfg.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
        cfg.authorityReleaseUnused=[](const std::vector<std::string>&){return false;};
    });
    CHECK(h.ready);if(!h.ready)return;
    h.server.stop();h.server.shutdown();CHECK_EQ(h.server.exitCode(),1);
}

TEST(shard_game_direct_shutdown_never_claims_pending_delta_was_saved) {
    Harness h("shard-stop-pending",{},dataDir(),0,[&](ServerConfig& cfg){
        cfg.authoritySnapshot="{}";
        cfg.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
        cfg.authorityDeltaCommit=[](const Json&){return Database::CommitOutcome::Pending;};
    });
    CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(connectClient(h,client));
    client.requestRegister("pendingstop","test-password-123");h.step(6,{&client});
    h.server.shutdown();CHECK_EQ(h.server.exitCode(),1);
}

TEST(shard_game_async_commit_defers_auth_without_stopping_simulation) {
    auto outcome=Database::CommitOutcome::Pending;Json submitted;int calls=0;
    Harness h("shard-async",{},dataDir(),0,[&](ServerConfig& cfg){
        cfg.authoritySnapshot="{}";
        cfg.authorityCommit=[](const std::string&){return Database::CommitOutcome::Committed;};
        cfg.authorityDeltaCommit=[&](const Json& delta){++calls;submitted=delta;return outcome;};
    });
    CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(connectClient(h,client));
    client.requestRegister("asyncaccount","test-password-123");
    h.step(8,{&client});
    CHECK(calls>0);CHECK(client.status()!=NetClient::Status::LoggedIn);
    CHECK(h.server.step());
    outcome=Database::CommitOutcome::Committed;
    CHECK(h.stepUntil({&client},[&]{return client.status()==NetClient::Status::LoggedIn;}));
    CHECK(submitted["users"].isObject());
    h.server.stop();for(int i=0;i<20&&h.server.step();++i){}
    h.server.shutdown();
    CHECK_EQ(h.server.exitCode(),0);
}

TEST(shard_transport_snapshots_skip_commit_but_mixed_economic_batch_waits) {
    struct ServerSide : net::TransportHandler {
        net::Connection* socket = nullptr;
        int commits = 0;
        bool allow = true;
        void onConnect(net::Connection& c) override {
            socket = &c;
            c.setWriteGate([this](bool durable) {
                if (durable) ++commits;
                return allow;
            }, [](const std::byte* data, std::size_t size) {
                return size && static_cast<net::ServerMessage>(std::to_integer<std::uint8_t>(data[0]))
                    != net::ServerMessage::Snapshot;
            });
        }
        void onMessage(net::Connection&, ByteReader&) override {}
    } server;
    struct ClientSide : net::TransportHandler {
        int frames = 0;
        void onMessage(net::Connection&, ByteReader&) override { ++frames; }
    } client;
    net::Listener listener; std::string error;
    std::uint16_t port = 47920;
    for (; port < 47940; ++port) if (listener.start(port, error)) break;
    CHECK(port < 47940); if (port == 47940) return;
    net::Dialer dialer;
    CHECK(dialer.connect("127.0.0.1", port, error));
    for (int i=0;i<20 && !server.socket;++i) { listener.poll(server,1); dialer.poll(client,1); }
    CHECK(server.socket != nullptr); if (!server.socket) return;
    ByteWriter snapshot; snapshot.u8(static_cast<std::uint8_t>(net::ServerMessage::Snapshot));
    ByteWriter profile; profile.u8(static_cast<std::uint8_t>(net::ServerMessage::Profile));
    server.socket->send(snapshot); listener.flush();
    for(int i=0;i<10 && client.frames<1;++i) dialer.poll(client,1);
    CHECK_EQ(client.frames,1); CHECK_EQ(server.commits,0);
    server.socket->send(snapshot); server.socket->send(profile); listener.flush();
    for(int i=0;i<10 && client.frames<3;++i) dialer.poll(client,1);
    CHECK_EQ(client.frames,3); CHECK_EQ(server.commits,1);
    server.allow=false; server.socket->send(profile); listener.flush();
    for(int i=0;i<5;++i) dialer.poll(client,1);
    CHECK_EQ(client.frames,3); CHECK_EQ(server.commits,2);
}

TEST(shard_game_disabled_keeps_local_account_persistence) {
    Harness h("shard-disabled", {}, dataDir(), 0);
    CHECK(h.ready); if (!h.ready) return;
    NetClient client;
    CHECK(loginNew(h, client, "localaccount", "test-password-123"));
    h.server.shutdown();
    Database reloaded; std::string error;
    CHECK(reloaded.load(h.dbPath, error));
    CHECK(reloaded.findUser("localaccount") != nullptr);
    CHECK(!reloaded.authoritative());
}

TEST(shard_game_registration_is_committed_before_auth_success) {
    std::string committed;
    int calls = 0;
    Harness h("shard-commit", {}, dataDir(), 0, [&](ServerConfig& cfg) {
        cfg.authoritySnapshot = "{}";
        cfg.authorityCommit = [&](const std::string& text) {
            committed = text; ++calls; return Database::CommitOutcome::Committed;
        };
    });
    CHECK(h.ready); if (!h.ready) return;
    NetClient client;
    CHECK(loginNew(h, client, "shardcommit", "test-password-123"));
    const auto disk = Json::parseOrNull(committed);
    CHECK(disk["users"].isObject());
    CHECK(!disk["users"]["shardcommit"].isNull());
    CHECK(disk["sessions"].size() > 0);
    CHECK(calls > 0);
    h.server.shutdown();
}

TEST(shard_game_unknown_commit_never_emits_auth_success_or_shutdown_retry) {
    auto result = Database::CommitOutcome::Committed;
    int calls = 0;
    Harness h("shard-unknown", {}, dataDir(), 0, [&](ServerConfig& cfg) {
        cfg.authoritySnapshot = "{}";
        cfg.authorityCommit = [&](const std::string&) { ++calls; return result; };
    });
    CHECK(h.ready); if (!h.ready) return;
    NetClient client;
    CHECK(connectClient(h, client));
    result = Database::CommitOutcome::Unknown;
    const int before = calls;
    client.requestRegister("shardunknown", "test-password-123");
    h.step(8, {&client});
    CHECK(client.status() != NetClient::Status::LoggedIn);
    CHECK_EQ(calls, before + 1);
    CHECK(!h.server.step());
    h.server.shutdown();
    CHECK_EQ(calls, before + 1);
}
