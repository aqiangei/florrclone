// The game server. Headless: it draws nothing and opens no window.
//
// Two builds, one program. Natively it owns its loop and listens on a TCP
// port. Under emscripten it runs on Node, listens for WebSocket -- and, when a
// certificate is configured, WebTransport -- sessions, and hands step() to a
// timer, because blocking the Node event loop is exactly what would stop every
// message from ever being delivered.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "server/game_server.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
namespace {
/// The running server, for flix_server_wake. Null before start() and again
/// once shutdown() has run.
flix::GameServer* g_wasmServer = nullptr;
}  // namespace

/// Handles what has just arrived, between two passes of the main loop. Called
/// from JavaScript only, from the hook armWakeOnArrival installs.
extern "C" EMSCRIPTEN_KEEPALIVE void flix_server_wake() {
    if (g_wasmServer) g_wasmServer->serviceArrivals();
}

EM_JS(void, flix_copy_shared_skins, (char* out), {
    HEAPU8.set(Module.flixSharedSkinSnapshot, out);
});

extern "C" EMSCRIPTEN_KEEPALIVE int flix_apply_shared_skins(int size) {
    if (!g_wasmServer || size <= 0 || size > 8 * 1024 * 1024) return 0;
    std::string json(static_cast<std::size_t>(size), '\0');
    flix_copy_shared_skins(json.data());
    return g_wasmServer->applySharedSkinSnapshot(flix::Json::parseOrNull(json)) ? 1 : 0;
}

EM_JS(int, flix_shared_skin_realm, (), {
    const realm = process.env.FLIX_SHARED_SKIN_REALM;
    return /^[123]$/.test(realm || '') ? Number(realm) : 0;
});

namespace {
std::unordered_map<unsigned, std::function<void(flix::Json)>> skinReviewRequests;
unsigned nextSkinReviewRequest = 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void flix_finish_skin_review(unsigned id, const char* body) {
    auto request = skinReviewRequests.extract(id);
    if (!request.empty() && g_wasmServer) request.mapped()(flix::Json::parseOrNull(body));
}

EM_JS(void, flix_read_skin_review_key, (char* out), {
    const key = process.env.FLIX_SKIN_REVIEW_KEY || '';
    stringToUTF8(/^[a-zA-Z0-9_-]{32,128}$/.test(key) ? key : '', out, 129);
});

EM_JS(void, flix_request_skin_review, (unsigned id, int realm, int local, const char* request), {
    const body = JSON.stringify({...JSON.parse(UTF8ToString(request)), realm, local});
    const finish = text => {
        const size = lengthBytesUTF8(text) + 1;
        const buffer = _malloc(size);
        try {
            stringToUTF8(text, buffer, size);
            _flix_finish_skin_review(id, buffer);
        } finally { _free(buffer); }
    };
    fetch('http://127.0.0.1:19571/internal/skin-review', {
        method: 'POST', redirect: 'error', signal: AbortSignal.timeout(8000),
        headers: {'Content-Type': 'application/json',
                  'Authorization': 'Bearer ' + process.env.FLIX_SKIN_REVIEW_KEY},
        body
    }).then(async response => {
        if (!response.ok) throw Error();
        const data = await response.text();
        if (data.length > 2 * 1024 * 1024) throw Error();
        return data;
    }).then(finish, () => finish(JSON.stringify({error: '跨服皮肤审核暂不可用，请刷新。'})));
});

EM_JS(void, flix_start_shared_skin_sync, (), {
    if (!process.env.FLIX_SHARED_SKIN_URL) return;
    let url;
    try { url = new URL(process.env.FLIX_SHARED_SKIN_URL); }
    catch (_) { console.error('[skins] Invalid shared gallery URL'); return; }
    if (url.protocol !== 'https:' && !(url.protocol === 'http:' && url.hostname === '127.0.0.1')) {
        console.error('[skins] Shared gallery requires HTTPS');
        return;
    }
    let running = false;
    let unavailable = false;
    let etag = '';
    const fetchGallery = async () => {
        if (running) return;
        running = true;
        try {
            const headers = etag ? {'If-None-Match': etag} : {};
            const response = await fetch(url, {headers, redirect: 'error', signal: AbortSignal.timeout(10_000)});
            if (response.status === 304) {
                unavailable = false;
                return;
            }
            if (!response.ok) throw Error();
            const chunks = [];
            let size = 0;
            for await (const chunk of response.body) {
                size += chunk.byteLength;
                if (size > 8 * 1024 * 1024) throw Error();
                chunks.push(chunk);
            }
            if (!size) throw Error();
            Module.flixSharedSkinSnapshot = Buffer.concat(chunks, size);
            try {
                if (_flix_apply_shared_skins(size)) etag = response.headers.get('etag') || '';
            } finally { delete Module.flixSharedSkinSnapshot; }
            if (unavailable) console.log('[skins] Shared gallery connection restored');
            unavailable = false;
        } catch (error) {
            if (!unavailable) console.warn('[skins] Shared gallery temporarily unavailable:', error.name);
            unavailable = true;
        } finally { running = false; }
    };
    fetchGallery();
    setInterval(fetchGallery, 30_000).unref();
});
#endif

namespace {

#ifndef __EMSCRIPTEN__
flix::GameServer* g_server = nullptr;

void onSignal(int) {
    // Only an atomic flag is stored here; flushing the database and telling
    // clients happens on the main thread, where it is safe to do.
    if (g_server) g_server->stop();
}
#endif

/// The last line the process prints, and the code it leaves behind. A restart
/// exits non-zero deliberately, because that is the one code every supervisor
/// restarts on. pm2's default autorestart would bring the process back after
/// any exit, but systemd's Restart=on-failure -- and pm2 with 0 in its
/// stop_exit_codes -- read exit 0 as "this one was meant to end" and leave the
/// server down, so the exit that is supposed to be followed by a start must
/// not look like a clean shutdown (GameServer::exitCode).
int reportExit(int code) {
    if (code == 0) {
        std::printf("shut down cleanly\n");
    } else {
        std::printf("stopped for restart; exiting with code %d so the supervisor starts us "
                    "again\n",
                    code);
    }
    return code;
}

void usage(const char* program) {
    std::printf(
        "usage: %s [options]\n"
        "  --port <number>    listen port (default 3000)\n"
        "  --data <dir>       the staged content: mobs.json, petals.json,\n"
        "                     mob_drops.json and the maps (default data)\n"
        "  --db <path>        account database (default inventory.json)\n"
        "  --seed <number>    simulation random seed\n"
        "  --bots <n>         bot population (0 = off, -1 = auto fill to 14)\n"
        "  --load-shed        reduce ambient mobs, distant snapshots and volley load\n"
        "  --tick-probe       log aligned tick, timer and save diagnostics\n"
        "  --no-slow-protection keep the 20 Hz target under load shedding\n"
#ifdef __EMSCRIPTEN__
        "  --cert <path>      TLS certificate. Without it, cert.crt then\n"
        "                     dev-cert.crt are looked for in the working\n"
        "                     directory; https also enables WebTransport\n"
        "  --key <path>       private key for --cert\n"
        "  --web-root <dir>   directory the client is served from (default:\n"
        "                     the build directory this module is in)\n"
#endif
        ,
        program);
}

#ifdef __EMSCRIPTEN__
/// Mounts the real directory holding `databasePath` over the same path in the
/// virtual filesystem, so the accounts file the server reads and writes is an
/// ordinary file on disk. Called before start(), which loads it. Rewrites
/// `databasePath` to the absolute path it resolved to.
///
/// The resolving is the point. Node`s working directory and the emscripten
/// filesystem`s are not the same place: the latter is always `/`, so a
/// relative --db -- and the default is the bare `inventory.json` that
/// `pm2 start server.js` gets -- names a file in memory that has nothing to do
/// with the one on disk of the same name. The server then loads no accounts,
/// saves to nowhere, and says nothing about either, which is how a database
/// comes to look wiped while sitting intact beside the process that is
/// ignoring it.
bool mountDatabaseDirectory(std::string& databasePath, std::string& errorOut) {
    // The caller owns the buffer, as in shared/net/web_channel.cpp: a path or
    // a failure message, and neither is worth exporting malloc for.
    char answer[1024] = {0};
    const int failed = EM_ASM_INT({
        const path = require("path");
        const fs = require("fs");
        const file = path.resolve(UTF8ToString($0));
        const directory = path.dirname(file);
        try {
            // The directory, not the file: --db may name one that does not
            // exist yet, which is an ordinary first run.
            fs.mkdirSync(directory, { recursive: true });
            FS.mkdirTree(directory);
            FS.mount(NODEFS, { root: directory }, directory);
        } catch (e) {
            stringToUTF8(directory + ": " + e, $1, $2);
            return 1;
        }
        stringToUTF8(file, $1, $2);
        return 0;
    }, databasePath.c_str(), answer, static_cast<int>(sizeof answer));

    if (failed) {
        errorOut = "the database directory cannot be reached from the host "
                   "filesystem (" + std::string(answer) + ")";
        return false;
    }
    databasePath = answer;
    return true;
}

/// Mounts the directory the database's backups go in, for the same reason as
/// the database's own, and by the same rule the writer uses
/// (Database::backupDirectoryFor): one level ABOVE the database's directory,
/// so it is not under that mount -- `/home/ubuntu/db_backups` beside
/// `/home/ubuntu/dist`. Left to MEMFS, every snapshot `backup_db` and
/// `update` took said it was saved to a real path and vanished at the next
/// restart, the one `update` takes it for included. Already inside the
/// database's directory -- a database one level below the root, whose backups
/// stay beside it -- it is the host's with no second mount.
///
/// Not fatal: a server without backups still serves its players. But nothing
/// relies on this having worked. Database::backup asks Node whether each
/// snapshot is really on the host's disk and refuses it when not, so a failed
/// mount is a refused `backup_db` and an aborted `update`, said in so many
/// words, and never a phantom. The mount is tried once, here, so the refusals
/// last until a restart finds the directory reachable: putting it right on
/// the host while the server runs mounts nothing.
void mountBackupDirectory(const std::string& databasePath) {
    const std::string backups = flix::Database::backupDirectoryFor(databasePath);
    const std::size_t slash = databasePath.find_last_of('/');
    const std::string directory =
        slash == std::string::npos ? std::string() : databasePath.substr(0, slash);
    if (!directory.empty() && backups.compare(0, directory.size() + 1, directory + "/") == 0) {
        return;
    }
    char answer[1024] = {0};
    const int failed = EM_ASM_INT({
        const fs = require("fs");
        const directory = UTF8ToString($0);
        try {
            fs.mkdirSync(directory, { recursive: true });
            FS.mkdirTree(directory);
            FS.mount(NODEFS, { root: directory }, directory);
        } catch (e) {
            stringToUTF8(directory + ": " + e, $1, $2);
            return 1;
        }
        return 0;
    }, backups.c_str(), answer, static_cast<int>(sizeof answer));
    if (failed) {
        std::fprintf(stderr,
                     "[db] the backup directory cannot be reached from the host filesystem "
                     "(%s); backup_db and update will refuse until the server is restarted "
                     "with it reachable\n",
                     answer);
    }
}

/// Has Node hand SIGINT and SIGTERM to this module instead of dying of them.
///
/// They are what `pm2 stop`, `pm2 restart` and a Ctrl-C send, and the native
/// build catches both with std::signal (onSignal, above). Under Node a C
/// signal handler is never called, and a process with no listener for one of
/// these is simply killed by it -- so the server never reached shutdown(), and
/// whatever its players had done since the last periodic save, up to half a
/// minute of it, went down with the process. The listener only NOTES the
/// signal, the way shared/net/web_channel.cpp's socket callbacks only note what
/// arrived: the main loop reads it on its next pass and makes the same stop()
/// the native handler makes, so the final save is written by the ordinary way
/// out.
EM_JS(void, flix_catch_stop_signals, (), {
    Module.flixStopSignal = "";
    const note = (name) => {
        if (!Module.flixStopSignal) Module.flixStopSignal = name;
    };
    process.on("SIGINT", () => note("SIGINT"));
    process.on("SIGTERM", () => note("SIGTERM"));
});

/// Whether one of those signals has arrived since the listeners went in.
EM_JS(int, flix_stop_signalled, (), { return Module.flixStopSignal ? 1 : 0; });

/// Has bytes landing on any socket run flix_server_wake, so what arrived is
/// answered at once rather than at the main loop's next pass. Waiting for
/// that pass used to add up to 17 ms of every round trip -- a ping, a login,
/// a craft. Once per turn of the event loop however
/// many sockets spoke in it: setImmediate runs after Node has dispatched all
/// the I/O it found, which is also what keeps the wake outside any callback
/// that is itself inside the module. Off again with 0.
EM_JS(void, flix_wake_on_arrival, (int on), {
    const net = Module.flixNet;
    if (!net) return;
    if (!on) { net.onDelivered = null; return; }
    let pending = false;
    net.onDelivered = () => {
        if (pending) return;
        pending = true;
        setImmediate(() => {
            pending = false;
            const probe = Module.flixTickHostProbe;
            if (!probe) { _flix_server_wake(); return; }
            const start = performance.now();
            _flix_server_wake();
            const work = performance.now() - start;
            ++probe.wakeCount;
            probe.wakeWork += work;
            probe.wakeMax = Math.max(probe.wakeMax, work);
        });
    };
});

EM_JS(void, flix_start_loop, (int probe), {
    const run = () => {
        const delay = _flix_server_step(-1);
        setTimeout(run, Math.max(1, Math.ceil(delay)));
    };
    if (!probe) { setTimeout(run, 1); return; }
    const host = {wakeCount: 0, wakeWork: 0, wakeMax: 0};
    Module.flixTickHostProbe = host;
    const schedule = delay => {
        const wait = Math.max(1, Math.ceil(delay));
        const due = performance.now() + wait;
        setTimeout(() => {
            const start = performance.now();
            const lag = Math.max(0, start - due);
            const report = lag >= 50 || host.wakeMax >= 50 ? {
                wallStartMs: performance.timeOrigin + start,
                timerLagMs: lag,
                wakeCount: host.wakeCount,
                wakeWorkMs: host.wakeWork,
                wakeMaxMs: host.wakeMax
            } : null;
            host.wakeCount = 0;
            host.wakeWork = 0;
            host.wakeMax = 0;
            const next = _flix_server_step(lag);
            schedule(next);
            if (report) console.log('[tick-probe-host] ' + JSON.stringify(report));
        }, wait);
    };
    schedule(1);
});
#endif

} // namespace

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE double flix_server_step(double timerLagMillis) {
    auto* running = g_wasmServer;
    if (flix_stop_signalled()) running->stop();
    if (running->step(timerLagMillis)) return running->nextStepDelayMillis();
    flix_wake_on_arrival(0);
    g_wasmServer = nullptr;
    running->shutdown();
    const int code = reportExit(running->exitCode());
    EM_ASM({ process.exit($0); }, code);
    return 0;
}
#endif

#ifdef __EMSCRIPTEN__
// Trusted host adapter, selected by the operator, never by a game packet.
// The synchronous C++ handler contract requires the adapter to return a final
// durability outcome; Promise/timeout/exception is not a successful commit.
EM_JS(int, flix_authority_init, (), {
    const filename = process.env.FLIX_AUTHORITY_MODULE;
    if (!filename) return 0;
    try {
        if (!require('path').isAbsolute(filename)) return -1;
        const authority = require(filename);
        if (!authority || typeof authority.snapshot !== 'function' || typeof authority.commit !== 'function') return -1;
        const snapshot = authority.snapshot();
        if (typeof snapshot !== 'string') return -1;
        Module.flixAuthority = authority;
        Module.flixAuthoritySnapshot = snapshot;
        return 1;
    } catch (e) { return -1; }
});
EM_JS(int, flix_authority_snapshot_size, (), {
    return Buffer.byteLength(Module.flixAuthoritySnapshot, 'utf8') + 1;
});
EM_JS(void, flix_authority_snapshot, (char* out, int capacity), {
    stringToUTF8(Module.flixAuthoritySnapshot, out, capacity);
    delete Module.flixAuthoritySnapshot;
});
EM_JS(int, flix_authority_commit, (const char* text), {
    try {
        const outcome = Module.flixAuthority.commit(UTF8ToString(text));
        if (outcome === 'committed') return 1;
        if (outcome === 'rejected') return 0;
        return 2;
    } catch (e) { return 2; }
});
EM_JS(int, flix_authority_has_delta, (), {
    return Module.flixAuthority && typeof Module.flixAuthority.commitDelta === 'function' ? 1 : 0;
});
EM_JS(int, flix_authority_has_invoke, (), {
    return Module.flixAuthority && typeof Module.flixAuthority.invoke === 'function' ? 1 : 0;
});
EM_JS(int, flix_authority_invoke_size, (const char* text), {
    try {
        const value=Module.flixAuthority.invoke(JSON.parse(UTF8ToString(text)));
        if(!value || typeof value.then==='function')return -1;
        Module.flixAuthorityReply=JSON.stringify(value);
        return Buffer.byteLength(Module.flixAuthorityReply,'utf8')+1;
    } catch(e) {return -1;}
});
EM_JS(void, flix_authority_invoke_copy, (char* out,int size), {
    stringToUTF8(Module.flixAuthorityReply,out,size);delete Module.flixAuthorityReply;
});
flix::Json invokeAuthority(const flix::Json& request) {
    const auto text=request.dump();const int size=flix_authority_invoke_size(text.c_str());
    if(size<=0||size>64*1024*1024)return flix::Json();
    std::string result(static_cast<std::size_t>(size),'\0');flix_authority_invoke_copy(result.data(),size);
    result.resize(static_cast<std::size_t>(size-1));return flix::Json::parseOrNull(result);
}
EM_JS(int, flix_authority_commit_delta, (const char* text), {
    try {
        const result=Module.flixAuthority.commitDelta(JSON.parse(UTF8ToString(text)));
        if(result==='committed')return 1;
        if(result==='rejected')return 0;
        if(result==='pending')return 3;
        return 2;
    } catch(e) {return 2;}
});
#endif

int main(int argc, char** argv) {
    // Line-buffer stdout. It is fully buffered by default whenever it is not a
    // terminal, which is every way the server is actually run -- under pm2, in
    // a redirect, in a CI log -- so the map summary and "listening on port"
    // sat in a 4KB buffer until the process exited. A server whose start-up
    // report only appears once it has stopped is a server nobody can check.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    flix::ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--port") config.port = static_cast<std::uint16_t>(std::atoi(next("--port")));
        else if (arg == "--data") config.dataDir = next("--data");
        else if (arg == "--db") config.databasePath = next("--db");
        else if (arg == "--seed") config.worldSeed = std::strtoull(next("--seed"), nullptr, 10);
        else if (arg == "--bots") config.botCount = std::atoi(next("--bots"));
        else if (arg == "--load-shed") config.loadShedding = true;
        else if (arg == "--no-slow-protection") config.adaptivePacing = false;
        else if (arg == "--background-save") config.backgroundSave = true;
        else if (arg == "--tick-probe") config.tickProbe = true;
        else if (arg == "--contact-visit-probe") {
            config.tickProbe = true;
            config.contactVisitProbe = true;
        }
        else if (arg == "--cert") config.certPath = next("--cert");
        else if (arg == "--key") config.keyPath = next("--key");
        else if (arg == "--web-root") config.webRoot = next("--web-root");
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

#ifdef __EMSCRIPTEN__
    // The embedded content is in memory and read-only, which is right for it.
    // The database is neither: it has to survive a restart, so the directory
    // it lives in is the real filesystem, mounted at the path the config
    // already names -- and so is the one its backups go in, which is not
    // under it. Everything else stays in MEMFS.
    std::string error;
    config.sharedSkinRealm = flix_shared_skin_realm();
    char skinReviewKey[129]{};
    flix_read_skin_review_key(skinReviewKey);
    config.skinReviewKey = skinReviewKey;
    if (config.sharedSkinRealm && !config.skinReviewKey.empty()) {
        config.sharedSkinReview = [local = config.sharedSkinRealm](
            int realm, const flix::Json& request, std::function<void(flix::Json)> callback) {
            if (skinReviewRequests.size() >= 32) {
                flix::Json response = flix::Json::object();
                response["error"] = "皮肤审核请求过多，请稍后刷新。";
                callback(std::move(response));
                return;
            }
            const auto id = ++nextSkinReviewRequest;
            skinReviewRequests.emplace(id, std::move(callback));
            flix_request_skin_review(id, realm, local, request.dump(0).c_str());
        };
    }
    const int authorityMode = flix_authority_init();
    if (authorityMode < 0) {
        std::fprintf(stderr, "could not initialize authoritative account adapter\n");
        return 1;
    }
    if (authorityMode > 0) {
        const int size = flix_authority_snapshot_size();
        if (size <= 0 || size > 256 * 1024 * 1024) return 1;
        config.authoritySnapshot.resize(static_cast<std::size_t>(size));
        flix_authority_snapshot(config.authoritySnapshot.data(), size);
        config.authoritySnapshot.resize(static_cast<std::size_t>(size - 1));
        config.authorityCommit = [](const std::string& text) {
            switch (flix_authority_commit(text.c_str())) {
                case 1: return flix::Database::CommitOutcome::Committed;
                case 0: return flix::Database::CommitOutcome::Rejected;
                default: return flix::Database::CommitOutcome::Unknown;
            }
        };
        if(flix_authority_has_delta())config.authorityDeltaCommit=[](const flix::Json& delta) {
            const auto text=delta.dump();
            switch(flix_authority_commit_delta(text.c_str())) {
                case 1:return flix::Database::CommitOutcome::Committed;
                case 0:return flix::Database::CommitOutcome::Rejected;
                case 3:return flix::Database::CommitOutcome::Pending;
                default:return flix::Database::CommitOutcome::Unknown;
            }
        };
        if(flix_authority_has_invoke()) {
            config.authorityHyper=true;
            config.authorityAuth=[](const flix::Json& request){return invokeAuthority(request);};
            config.authorityReleaseAsync=[](const std::vector<std::string>& active) {
                flix::Json request=flix::Json::object();request["action"]="releaseUnusedAsync";request["active"]=flix::Json::array();
                for(const auto& account:active)request["active"].push(account);
                return invokeAuthority(request);
            };
        }
    }
    // Fatal, where it used to be a warning. Every account on this server is in
    // that one file; starting without it would serve an empty world and then
    // persist nothing, and both halves of that are silent.
    if (!mountDatabaseDirectory(config.databasePath, error)) {
        std::fprintf(stderr, "could not start: %s\n", error.c_str());
        return 1;
    }
    mountBackupDirectory(config.databasePath);

    // Leaked deliberately: main() returns as soon as the timer is armed and
    // the server has to outlive it. Node exiting is this process's exit.
    auto* server = new flix::GameServer();
    if (!server->start(config, error)) {
        std::fprintf(stderr, "could not start: %s\n", error.c_str());
        EM_ASM({ process.exit(1); });
        return 1;
    }
    std::printf("listening on port %u\n", static_cast<unsigned>(config.port));
    // Once the server is up, and before its first pass: a signal before this
    // point has nothing to save, and may end the process the way it always
    // has.
    flix_catch_stop_signals();
    g_wasmServer = server;
    if (config.sharedSkinRealm) flix_start_shared_skin_sync();
    flix_wake_on_arrival(1);
    flix_start_loop(config.tickProbe);
    return 0;
#else
    flix::GameServer server;
    std::string error;
    if (!server.start(config, error)) {
        std::fprintf(stderr, "could not start: %s\n", error.c_str());
        return 1;
    }

    // A write to a closed socket must not take the process down; the transport
    // reports the failure and drops that one connection instead.
    std::signal(SIGPIPE, SIG_IGN);

    g_server = &server;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::printf("listening on port %u\n", static_cast<unsigned>(config.port));
    server.run();
    return reportExit(server.exitCode());
#endif
}
