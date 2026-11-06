#pragma once
// Internal, trusted-node coordination kernel. This does not enable game sharding.
// Authentication, freeze-before-prepare and gate-before-spawn belong to adapters.
#include "shared/core/json.h"
#include "server/crypto.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

namespace flix::shards {
enum class Durability { NotCommitted, Committed, Unknown };
enum class Role { Game, Supervisor };
// Role is supplied by authenticated transport configuration, never request JSON.
struct Node { std::string shard, boot; Role role=Role::Game; };

inline Json canonical(const Json& value) {
    if(value.isObject()){Json out=Json::object();for(const auto& kv:value.members())out[kv.first]=canonical(kv.second);return out;}
    if(value.isArray()){Json out=Json::array();for(std::size_t i=0;i<value.size();++i)out.push(canonical(value[i]));return out;}
    return value;
}
inline bool identifier(const std::string& text) {
    if(text.empty()||text.size()>128)return false;
    for(unsigned char c:text)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    return true;
}
inline bool counter(const Json& value) {
    const double n=value.asDouble(-1);
    return value.isNumber()&&std::isfinite(n)&&n>=0&&n<9007199254740000.0&&std::floor(n)==n;
}
inline Json failure(const char* code) {Json r=Json::object();r["ok"]=false;r["error"]=code;return r;}
inline Json success() {Json r=Json::object();r["ok"]=true;return r;}

class Authority {
public:
    // Committed means fsync+rename+directory fsync completed, not just fwrite.
    using Persist = std::function<Durability(const Json&)>;
    struct DeltaJournal {};
    Authority(Json state, Persist persist):state_(std::move(state)),persist_(std::move(persist)) {
        poisoned_=!validState(state_)||!persist_;
    }
    // In journal mode Persist receives a versioned delta, not a full snapshot.
    // Use recoverDelta when replaying that journal before constructing us.
    Authority(Json state, DeltaJournal, Persist persist)
        :state_(std::move(state)),persist_(std::move(persist)),deltaMode_(true) {
        poisoned_=!validState(state_)||!persist_;
    }
    static Json initial(const Json& records, const Json& globals=Json::object()) {
        Json state=Json::object();state["schema"]=1;state["sequence"]=0;state["accounts"]=Json::object();
        state["globalRevision"]=0;state["globals"]=globals;state["requests"]=Json::object();
        state["receipts"]=Json::object();
        for(const auto& kv:records.members()) {
            Json a=Json::object();a["record"]=kv.second;a["revision"]=0;a["epoch"]=0;
            a["phase"]="offline";a["shard"]="";a["boot"]="";state["accounts"][kv.first]=a;
        }
        return state;
    }
    // Fail closed on malformed recovery data. Do not silently start an empty authority.
    static bool validState(const Json& s) {
        if(s["schema"].asDouble()!=1||!counter(s["sequence"])||!counter(s["globalRevision"])||
           !s["accounts"].isObject()||!s["globals"].isObject()||!s["requests"].isObject())return false;
        if(!s["receipts"].isNull()&&!s["receipts"].isObject())return false;
        for(const auto& kv:s["receipts"].members())if(!actorKey(kv.first)||!counter(kv.second))return false;
        for(const auto& kv:s["accounts"].members()) {
            const auto& a=kv.second;const auto phase=a["phase"].asString();
            if(!identifier(kv.first)||!a["record"].isObject()||!counter(a["epoch"])||!counter(a["revision"]))return false;
            if(phase=="offline") {if(!a["shard"].asString().empty()||!a["boot"].asString().empty())return false;}
            else if(phase=="active"||phase=="prepared") {
                if(!identifier(a["shard"].asString())||!identifier(a["boot"].asString()))return false;
                if(phase=="prepared"&&(!identifier(a["pending"]["id"].asString())||!identifier(a["pending"]["target"].asString())))return false;
            } else return false;
        }
        for(const auto& kv:s["requests"].members()) {
            if(!kv.second["digest"].isString()||!kv.second["result"]["ok"].asBool())return false;
            if(!kv.second["serial"].isNull()) {
                const auto& r=kv.second;
                if(!counter(r["serial"])||r["serial"].asDouble()<1||!actorKey(r["actor"].asString())||
                   kv.first!=serialKey(r["actor"].asString(),r["serial"].asDouble())||
                   r["serial"].asDouble()<=s["receipts"][r["actor"].asString()].asDouble())return false;
            }
        }
        return true;
    }
    Json snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
    Json inspect(const std::function<Json(const Json&)>& view) const {std::lock_guard<std::mutex> lock(mutex_);return poisoned_?failure("authority_frozen"):view(state_);}
    bool healthy() const {std::lock_guard<std::mutex> lock(mutex_);return !poisoned_;}

    static bool recoverDelta(Json& state,const Json& delta) {
        if(delta["schema"].asInt()!=1||!counter(delta["sequence"])||
           delta["sequence"].asDouble()!=state["sequence"].asDouble()+1||
           !delta["accounts"].isObject()||!delta["requests"].isObject()||delta["requests"].size()!=1||
           !counter(delta["globalRevision"]))return false;
        Json check=initial(delta["accounts"]);
        check["accounts"]=delta["accounts"];
        check["requests"]=delta["requests"];
        check["receipts"]=state["receipts"].isObject()?state["receipts"]:Json::object();
        for(const auto& kv:delta["receipts"].members())check["receipts"][kv.first]=kv.second;
        if(!validState(check))return false;
        const bool global=delta["globals"].isObject(),patch=delta["globalPatch"].isObject();
        if((!global&&!delta["globals"].isNull())||(!patch&&!delta["globalPatch"].isNull())||
            (global&&patch)||(patch&&!validPatch(delta["globalPatch"]))||delta["globalRevision"].asDouble()!=
            state["globalRevision"].asDouble()+((global||patch)?1:0))return false;
        if(!delta["receipts"].isNull()&&!delta["receipts"].isObject())return false;
        for(const auto& kv:delta["receipts"].members())
            if(!actorKey(kv.first)||!counter(kv.second)||kv.second.asDouble()<static_cast<const Json&>(state)["receipts"][kv.first].asDouble())return false;
        for(const auto& kv:delta["requests"].members())if(!static_cast<const Json&>(state)["requests"][kv.first].isNull())return false;
        for(const auto& kv:delta["accounts"].members())
            if(static_cast<const Json&>(state)["accounts"][kv.first].isNull()&&
               !delta["created"][kv.first].asBool())return false;
        for(const auto& kv:delta["accounts"].members())state["accounts"][kv.first]=kv.second;
        for(const auto& kv:delta["requests"].members())state["requests"][kv.first]=kv.second;
        for(const auto& kv:delta["receipts"].members())acknowledge(state,kv.first,kv.second.asDouble());
        if(global)state["globals"]=delta["globals"];
        if(patch)applyPatch(state["globals"],delta["globalPatch"]);
        state["sequence"]=delta["sequence"];state["globalRevision"]=delta["globalRevision"];
        return true;
    }

    // Trusted in-process planner for council transactions. Deduplication hashes
    // the original request, before planning, so retries cannot publish twice.
    using Prepare = std::function<Json(const Json&)>;
    Json apply(const Node& node,const Json& request,const Prepare& prepare={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(poisoned_)return failure("authority_frozen");
        if(!identifier(node.shard)||!identifier(node.boot)||!request.isObject())return failure("bad_node_or_request");
        if(request["op"].asString()=="read_identity")return readIdentity(state_,request);
        if(request["op"].asString()=="read_globals")return readGlobals(state_,request);
        if(request["op"].asString()=="read_owned") {
            Json reply=success();reply["accounts"]=Json::array();
            for(const auto& row:state_["accounts"].members())if(row.second["shard"].asString()==node.shard&&
                row.second["boot"].asString()==node.boot&&row.second["phase"].asString()!="offline")reply["accounts"].push(row.first);
            return reply;
        }
        const auto id=request["requestId"].asString();
        if(!identifier(id))return failure("bad_request_id");
        const auto actor=(node.role==Role::Supervisor?"supervisor/":"game/")+node.shard+"/"+node.boot;
        const bool numbered=!request["serial"].isNull();
        const double floor=static_cast<const Json&>(state_)["receipts"][actor].asDouble();
        double serial=0,ack=floor;
        if(numbered){
            if(!counter(request["serial"])||request["serial"].asDouble()<1||
               (!request["ackThrough"].isNull()&&!counter(request["ackThrough"])))return failure("bad_request_serial");
            serial=request["serial"].asDouble();
            if(serial<=floor)return failure("request_acknowledged");
            ack=std::max(floor,request["ackThrough"].asDouble());
            if(ack>=serial)return failure("bad_acknowledgement");
        }else if(!request["ackThrough"].isNull())return failure("bad_acknowledgement");
        const auto key=numbered?serialKey(actor,serial):actor+"/"+id;
        const auto digest=crypto::sha256Hex(canonical(request).dump());
        const auto& prior=static_cast<const Json&>(state_)["requests"][key];
        if(!prior.isNull()) {
            if(prior["digest"].asString()!=digest)return failure("request_id_reused");
            Json reply=prior["result"];reply["replayed"]=true;
            const auto& current=static_cast<const Json&>(state_)["accounts"][request["account"].asString()];
            reply["currentOwnership"]=current["phase"].asString()=="active"&&current["shard"].asString()==node.shard&&
                current["boot"].asString()==node.boot&&reply["epoch"].isNumber()&&reply["epoch"].asDouble()==current["epoch"].asDouble();
            return reply;
        }
        // Only replies explicitly acknowledged by their own authenticated
        // actor may be discarded. The durable floor rejects old serials after
        // compaction/restart, so pruning cannot re-execute an old transaction.
        std::size_t retained=state_["requests"].size();
        if(numbered)for(const auto& kv:state_["requests"].members())
            if(kv.second["actor"].asString()==actor&&counter(kv.second["serial"])&&kv.second["serial"].asDouble()<=ack)--retained;
        if(retained>=4096)return failure("request_journal_full");
        if((request["op"].asString()=="acquire"||request["op"].asString()=="reaffirm")&&request["identityKey"].isString()) {
            const auto& identity=static_cast<const Json&>(state_)["globals"]["users"][request["identityKey"].asString()];
            if(identity["id"].asString()!=request["account"].asString()||
               (request["expectedIdentity"].isObject()?canonical(identity).dump()!=canonical(request["expectedIdentity"]).dump():
                crypto::sha256Hex(canonical(identity).dump())!=request["identityHash"].asString()))return failure("identity_changed");
            if(request["sessionHash"].isString()) {
                const auto& session=static_cast<const Json&>(state_)["globals"]["sessions"][request["sessionHash"].asString()];
                if(session["userId"].asString()!=request["account"].asString()||
                   (request["expectedSession"].isObject()?canonical(session).dump()!=canonical(request["expectedSession"]).dump():
                    crypto::sha256Hex(canonical(session).dump())!=request["sessionRecordHash"].asString()))return failure("session_changed");
            }
        }
        Json planned;
        if(prepare) {
            planned=prepare(state_);
            if(planned["op"].asString()!="global_commit"&&planned["op"].asString()!="global_patch"&&planned["op"].asString()!="coordinated_patch")return failure("transaction_preparation_rejected");
        }
        const Json& logical=prepare?planned:request;
        const bool coordinated=prepare&&logical["op"].asString()=="coordinated_patch";
        if(!prepare&&logical["op"].asString()=="coordinated_patch")return failure("trusted_planner_required");
        Json translated;
        const bool patchMode=logical["op"].asString()=="global_patch";
        if(patchMode){
            if(!validPatch(logical["patch"])||!logical["writes"].isArray()||!logical["globals"].isNull())return failure("bad_global_patch");
            translated=logical;translated["op"]="global_commit";
            translated["globals"]=deltaMode_?patchBase(state_["globals"],logical["patch"],logical["writes"]):state_["globals"];
            applyPatch(translated["globals"],logical["patch"]);
        }
        const Json& command=patchMode?translated:logical;
        Json next;
        if(deltaMode_) {
            next=Json::object();next["schema"]=1;next["sequence"]=state_["sequence"];
            next["globalRevision"]=state_["globalRevision"];next["accounts"]=Json::object();
            next["requests"]=Json::object();
            if(coordinated) {
                next["accounts"]=logical["accounts"];
                next["globalPatch"]=logical["patch"];
            } else if(command["op"].asString()=="global_commit"||command["op"].asString()=="checkpoint_batch") {
                if(!command["writes"].isArray())return failure("bad_transaction");
                if(command["op"].asString()=="global_commit")next["globals"]=patchMode?
                    patchBase(state_["globals"],logical["patch"],logical["writes"]):state_["globals"];
                for(std::size_t i=0;i<command["writes"].size();++i) {
                    const auto& write=command["writes"][i];
                    const auto account=write["account"].asString();
                    next["accounts"][account]=static_cast<const Json&>(state_)["accounts"][account];
                }
            } else {
                const auto account=command["account"].asString();
                next["accounts"][account]=static_cast<const Json&>(state_)["accounts"][account];
            }
        } else next=state_;
        if(numbered){
            if(deltaMode_)next["receipts"][actor]=ack;
            else acknowledge(next,actor,ack);
        }
        Json reply;
        if(coordinated) {
            if(!logical["accounts"].isObject()||!validPatch(logical["patch"]))return failure("bad_coordinated_patch");
            for(const auto& row:logical["accounts"].members())next["accounts"][row.first]=row.second;
            if(!deltaMode_)applyPatch(next["globals"],logical["patch"]);
            if(!increment(next["globalRevision"]))return failure("counter_exhausted");
            reply=logical["reply"];reply["ok"]=true;
        } else reply=execute(next,node,command);
        if(!reply["ok"].asBool())return reply;
        if(patchMode&&deltaMode_){next.erase("globals");next["globalPatch"]=logical["patch"];}
        if(!increment(next["sequence"]))return failure("counter_exhausted");
        reply["sequence"]=next["sequence"];
        Json saved=Json::object();saved["digest"]=digest;saved["result"]=reply;
        if(numbered){saved["actor"]=actor;saved["serial"]=serial;}
        next["requests"][key]=saved;
        Durability durable=Durability::Unknown;
        try {durable=persist_(next);}catch(...){poisoned_=true;return failure("commit_unknown");}
        if(durable==Durability::NotCommitted)return failure("save_failed");
        if(durable==Durability::Unknown){poisoned_=true;return failure("commit_unknown");}
        if(deltaMode_) {
            if(!recoverDelta(state_,next)){poisoned_=true;return failure("commit_unknown");}
        } else state_=std::move(next);
        return reply;
    }
private:
    static Json readGlobals(const Json& state,const Json& query) {
        if(!query["keys"].isObject())return failure("bad_global_query");
        Json reply=success();reply["globalRevision"]=state["globalRevision"];reply["patch"]=Json::object();
        for(const auto* table:{"users","sessions","globals"})reply["patch"][table]=Json::object();
        std::size_t count=0;
        for(const auto& group:query["keys"].members()) {
            if((group.first!="users"&&group.first!="sessions"&&group.first!="globals")||!group.second.isArray())return failure("bad_global_query");
            for(const auto& field:group.second.items()) {
                const auto key=field.asString();
                if(++count>1024||key.empty()||key.size()>512||
                    (group.first=="globals"&&(key=="users"||key=="sessions"||key=="players")))return failure("bad_global_query");
                reply["patch"][group.first][key]=group.first=="globals"?state["globals"][key]:state["globals"][group.first][key];
            }
        }
        return reply;
    }
    static bool validPatch(const Json& patch){
        if(!patch.isObject())return false;
        for(const auto& group:patch.members()){
            if((group.first!="users"&&group.first!="sessions"&&group.first!="globals")||!group.second.isObject())return false;
            for(const auto& row:group.second.members()){
                if(row.first.empty()||row.first.size()>512)return false;
                if(group.first=="globals"){
                    if(row.first=="users"||row.first=="sessions"||row.first=="players")return false;
                }else if(!row.second.isNull()&&!row.second.isObject())return false;
            }
        }
        return true;
    }
    static void applyPatch(Json& globals,const Json& patch){
        for(const auto* table:{"users","sessions"})if(patch[table].isObject()){
            if(!globals[table].isObject())globals[table]=Json::object();
            for(const auto& row:patch[table].members()){
                if(row.second.isNull())globals[table].erase(row.first);else globals[table][row.first]=row.second;
            }
        }
        for(const auto& row:patch["globals"].members()){
            if(row.second.isNull())globals.erase(row.first);else globals[row.first]=row.second;
        }
    }
    static Json patchBase(const Json& globals,const Json& patch,const Json& writes){
        Json selected=Json::object();bool creates=false;
        for(std::size_t i=0;i<writes.size();++i)creates=creates||writes[i]["create"].asBool();
        // Registration needs the full username index for case-insensitive
        // collision detection, but never unrelated migration/history payloads.
        if(creates)selected["users"]=globals["users"];
        for(const auto* table:{"users","sessions"})if(patch[table].isObject()){
            if(!selected[table].isObject())selected[table]=Json::object();
            for(const auto& row:patch[table].members())if(!globals[table][row.first].isNull())selected[table][row.first]=globals[table][row.first];
        }
        for(const auto& row:patch["globals"].members())if(!globals[row.first].isNull())selected[row.first]=globals[row.first];
        return selected;
    }
    static Json readIdentity(const Json& state,const Json& query){
        const auto username=query["username"].asString(),hash=query["sessionHash"].asString();
        if((username.empty()==hash.empty())||username.size()>128||hash.size()>512)return failure("bad_identity_query");
        Json reply=success();reply["found"]=false;
        const auto& users=state["globals"]["users"];
        std::string identityKey;
        if(!hash.empty()){
            const auto& session=state["globals"]["sessions"][hash];
            const auto account=session["userId"].asString();
            if(account.empty())return reply;
            for(const auto& row:users.members())if(row.second["id"].asString()==account){identityKey=row.first;break;}
            if(identityKey.empty())return reply;
            reply["session"]=session;
        }else{
            if(users[username].isObject())identityKey=username;
            else{
                auto lower=[](std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;};
                const auto wanted=lower(username);
                for(const auto& row:users.members())if(lower(row.first)==wanted){identityKey=row.first;break;}
            }
            if(identityKey.empty())return reply;
        }
        const auto& identity=users[identityKey];const auto account=identity["id"].asString();
        const auto& row=state["accounts"][account];
        if(!row["record"].isObject())return reply;
        reply["found"]=true;reply["identityKey"]=identityKey;reply["identity"]=identity;
        reply["account"]=account;reply["record"]=row["record"];
        Json ownership=Json::object();
        for(const auto* field:{"phase","shard","boot","epoch","revision","pending"})if(!row[field].isNull())ownership[field]=row[field];
        reply["ownership"]=ownership;return reply;
    }
    static bool actorKey(const std::string& value){
        const auto a=value.find('/'),b=a==std::string::npos?a:value.find('/',a+1);
        return a!=std::string::npos&&b!=std::string::npos&&value.find('/',b+1)==std::string::npos&&
            (value.substr(0,a)=="game"||value.substr(0,a)=="supervisor")&&
            identifier(value.substr(a+1,b-a-1))&&identifier(value.substr(b+1));
    }
    static std::string serialKey(const std::string& actor,double serial){return actor+"/#"+std::to_string(static_cast<std::uint64_t>(serial));}
    static void acknowledge(Json& state,const std::string& actor,double floor){
        state["receipts"][actor]=floor;std::vector<std::string> erase;
        for(const auto& kv:state["requests"].members())if(kv.second["actor"].asString()==actor&&
            counter(kv.second["serial"])&&kv.second["serial"].asDouble()<=floor)erase.push_back(kv.first);
        for(const auto& key:erase)state["requests"].erase(key);
    }
    static bool increment(Json& n) {if(!counter(n))return false;const double next=n.asDouble()+1;if(next>=9007199254740000.0)return false;n=next;return true;}
    static bool holds(const Json& a,const Node& node,const Json& req) {
        return !a["hyperLock"]["ready"].asBool()&&a["phase"].asString()=="active"&&a["shard"].asString()==node.shard&&a["boot"].asString()==node.boot&&
               counter(req["epoch"])&&req["epoch"].asDouble()==a["epoch"].asDouble()&&
               counter(req["revision"])&&req["revision"].asDouble()==a["revision"].asDouble();
    }
    static Json grant(Json& a,const Node& node) {
        if(!increment(a["epoch"]))return failure("counter_exhausted");
        a["phase"]="active";a["shard"]=node.shard;a["boot"]=node.boot;a.erase("pending");
        Json r=success();r["epoch"]=a["epoch"];r["revision"]=a["revision"];r["record"]=a["record"];return r;
    }
    static Json execute(Json& state,const Node& node,const Json& req) {
        const auto op=req["op"].asString();
        if(op=="checkpoint_batch"){
            const auto& writes=req["writes"];std::set<std::string> seen;
            if(!writes.isArray()||writes.size()==0||writes.size()>1024||!req["globals"].isNull())return failure("bad_transaction");
            for(std::size_t i=0;i<writes.size();++i){
                const auto id=writes[i]["account"].asString();
                if(!identifier(id)||!seen.insert(id).second||!writes[i]["record"].isObject()||writes[i]["create"].asBool())return failure("bad_transaction");
                if(!holds(static_cast<const Json&>(state)["accounts"][id],node,writes[i]))return failure("account_conflict");
            }
            Json reply=success();reply["revisions"]=Json::object();reply["epochs"]=Json::object();
            for(std::size_t i=0;i<writes.size();++i){
                const auto id=writes[i]["account"].asString();auto& a=state["accounts"][id];
                a["record"]=writes[i]["record"];if(!increment(a["revision"]))return failure("counter_exhausted");
                reply["revisions"][id]=a["revision"];reply["epochs"][id]=a["epoch"];
            }
            return reply;
        }
        if(op=="global_commit") {
            // Any account touched by H/other global operations must be frozen and
            // exclusively leased to this actor first. Never modify a foreign live copy.
            if(!counter(req["globalRevision"])||req["globalRevision"].asDouble()!=state["globalRevision"].asDouble())return failure("global_conflict");
            const auto& writes=req["writes"];
            if(!writes.isArray()||writes.size()>1024||!req["globals"].isObject())return failure("bad_transaction");
            std::set<std::string> seen,newNames;
            for(std::size_t i=0;i<writes.size();++i) {
                const auto id=writes[i]["account"].asString();
                if(!identifier(id)||!seen.insert(id).second||!writes[i]["record"].isObject())return failure("bad_transaction");
                if(static_cast<const Json&>(state)["accounts"][id].isNull()&&writes[i]["create"].asBool()) {
                    const auto username=writes[i]["username"].asString();
                    const auto& user=req["globals"]["users"][username];
                    if(username.empty()||user["id"].asString()!=id||
                       !static_cast<const Json&>(state)["globals"]["users"][username].isNull())return failure("account_creation_conflict");
                    std::string lower=username;
                    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                    if(!newNames.insert(lower).second)return failure("account_creation_conflict");
                    for(const auto& existing:state["globals"]["users"].members()) {
                        std::string other=existing.first;
                        std::transform(other.begin(),other.end(),other.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                        if(other==lower)return failure("account_creation_conflict");
                    }
                    Json a=Json::object();a["record"]=writes[i]["record"];a["epoch"]=0;a["revision"]=0;
                    a["phase"]="offline";a["shard"]="";a["boot"]="";
                    state["accounts"][id]=std::move(a);grant(state["accounts"][id],node);state["created"][id]=true;
                    continue;
                }
                if(!holds(static_cast<const Json&>(state)["accounts"][id],node,writes[i]))return failure("account_conflict");
            }
            // Every changed registered H owner participates in the same fenced transaction.
            const auto& before=static_cast<const Json&>(state)["globals"]["hyperForges"];
            const auto& after=req["globals"]["hyperForges"];
            std::set<std::string> keys;for(const auto& kv:before.members())keys.insert(kv.first);for(const auto& kv:after.members())keys.insert(kv.first);
            for(const auto& key:keys)if(canonical(before[key]).dump()!=canonical(after[key]).dump()) {
                for(const auto& owner:{before[key]["owner"].asString(),after[key]["owner"].asString()})
                    if(!owner.empty()&&!seen.count(owner))return failure("hyper_owner_not_locked");
            }
            Json versions=Json::object(),epochs=Json::object();
            for(std::size_t i=0;i<writes.size();++i) {
                auto id=writes[i]["account"].asString();auto& a=state["accounts"][id];
                a["record"]=writes[i]["record"];if(!increment(a["revision"]))return failure("counter_exhausted");versions[id]=a["revision"];epochs[id]=a["epoch"];
            }
            state["globals"]=req["globals"];if(!increment(state["globalRevision"]))return failure("counter_exhausted");
            Json r=success();r["globalRevision"]=state["globalRevision"];r["revisions"]=versions;r["epochs"]=epochs;return r;
        }
        const auto id=req["account"].asString();
        if(!identifier(id)||static_cast<const Json&>(state)["accounts"][id].isNull())return failure("unknown_account");
        auto& a=state["accounts"][id];
        if(op=="reaffirm") {
            if(!holds(a,node,req))return failure("account_conflict");
            Json reply=success();reply["epoch"]=a["epoch"];reply["revision"]=a["revision"];reply["record"]=a["record"];return reply;
        }
        if(op=="recover_stopped") {
            // Only after the supervisor has verified termination of the old process.
            // Network silence or elapsed time is explicitly insufficient evidence.
            if(node.role!=Role::Supervisor)return failure("supervisor_required");
            if(a["phase"].asString()=="offline"||a["shard"].asString()!=req["stoppedShard"].asString()||
               a["boot"].asString()!=req["stoppedBoot"].asString()||!counter(req["epoch"])||
               a["epoch"].asDouble()!=req["epoch"].asDouble())return failure("recovery_conflict");
            if(!increment(a["epoch"]))return failure("counter_exhausted");
            a["phase"]="offline";a["shard"]="";a["boot"]="";a.erase("pending");
            Json r=success();r["epoch"]=a["epoch"];r["revision"]=a["revision"];return r;
        }
        if(op=="acquire") {
            if(a["hyperLock"].isObject())return failure("account_busy");
            if(a["phase"].asString()!="offline")return failure("account_busy");
            return grant(a,node);
        }
        if(op=="claim") {
            if(a["hyperLock"].isObject())return failure("account_busy");
            const auto& pending=static_cast<const Json&>(a)["pending"];
            if(a["phase"].asString()!="prepared"||pending["target"].asString()!=node.shard||
               !identifier(req["transfer"].asString())||pending["id"].asString()!=req["transfer"].asString())return failure("invalid_transfer");
            return grant(a,node);
        }
        if(op=="cancel") {
            const auto& pending=static_cast<const Json&>(a)["pending"];
            if(a["phase"].asString()!="prepared"||a["shard"].asString()!=node.shard||a["boot"].asString()!=node.boot||
               pending["id"].asString()!=req["transfer"].asString())return failure("invalid_transfer");
            return grant(a,node);
        }
        if(op!="checkpoint"&&op!="prepare"&&op!="release")return failure("unknown_operation");
        if(!holds(a,node,req))return failure("account_conflict");
        if(!req["record"].isObject())return failure("bad_record");
        if(op=="prepare") {
            const auto target=req["target"].asString(),transfer=req["transfer"].asString();
            if(!identifier(target)||target==node.shard||!identifier(transfer))return failure("bad_target_or_transfer");
            // Transfer IDs must never be reused for the account, even after cancellation.
            if(!a["transfers"][transfer].isNull())return failure("transfer_reused");
            a["transfers"][transfer]=true;
            if(!increment(a["epoch"]))return failure("counter_exhausted");
            a["phase"]="prepared";Json pending=Json::object();pending["id"]=transfer;pending["target"]=target;a["pending"]=pending;
        } else if(op=="release") {
            if(!increment(a["epoch"]))return failure("counter_exhausted");
            a["phase"]="offline";a["shard"]="";a["boot"]="";
        }
        a["record"]=req["record"];if(!increment(a["revision"]))return failure("counter_exhausted");
        Json reply=success();reply["epoch"]=a["epoch"];reply["revision"]=a["revision"];reply["phase"]=a["phase"];return reply;
    }
    mutable std::mutex mutex_;
    Json state_;
    Persist persist_;
    bool poisoned_=false;
    bool deltaMode_=false;
};
} // namespace flix::shards
