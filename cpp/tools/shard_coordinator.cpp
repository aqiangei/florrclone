// Local pipe endpoint. Only the authenticated loopback front-end may own stdin.
#include "server/shard_journal.h"
#include "server/shard_council.h"
#include "server/shard_hyper_barrier.h"
#include "server/shard_public_view.h"
#include "server/shard_presence.h"
#include "server/daily_drop_bonus.h"
#include <chrono>
#include <iostream>
using namespace flix;
using namespace flix::shards;
int main(int argc,char** argv) {
    const bool pauseBonus=argc==6&&std::string(argv[3])=="--pause-bonus";
    if(argc!=3&&!pauseBonus){std::cerr<<"usage: shard_coordinator STATE_BASE BOOTSTRAP_INVENTORY [--pause-bonus ID STOPPED_MS]\n";return 2;}
    Json inventory,initialState;std::string error;
    // Existing authority is the source of truth. Do not parse and clone the
    // original 20+ MB bootstrap again on every restart. A null bootstrap makes
    // disappearance of the observed snapshot fail closed in JournalStore.
    if(!std::ifstream(std::string(argv[1])+".snapshot",std::ios::binary).good()) {
    if(!Json::parseFile(argv[2],inventory,error)||!inventory.isObject()||
       !inventory["players"].isObject()||!inventory["users"].isObject()) {
        std::cerr<<"invalid bootstrap inventory\n";return 2;
    }
    const auto players=inventory["players"];inventory.erase("players");
    if(inventory["ipSalt"].asString().empty())inventory["ipSalt"]=crypto::secureRandom().hex(32);
    if(!inventory["bossClocks"].isNull()) {
        for(const auto* shard:{"one","two"})inventory["shardWorlds"][shard]["bossClocks"]=inventory["bossClocks"];
        inventory.erase("bossClocks");
    }
    initialState=Authority::initial(players,inventory);
    }
    JournalStore store(argv[1]);Json state;
    if(!store.open(initialState,state,error)) {
        std::cerr<<"coordinator recovery failed: "<<error<<"\n";return 1;
    }
    inventory=Json();initialState=Json();
    Json committedDelta;
    Authority authority(std::move(state),Authority::DeltaJournal{},[&](const Json& delta){
        const auto result=store.append(delta);if(result==Durability::Committed)committedDelta=delta;return result;
    });
    if(pauseBonus) {
        const std::string id=argv[4];std::int64_t stoppedAt=0;
        try{std::size_t used=0;stoppedAt=std::stoll(argv[5],&used);if(used!=std::string(argv[5]).size())return 2;}catch(...){return 2;}
        if(!identifier(id)||stoppedAt<=0)return 2;
        Json request=Json::object();request["op"]="maintenance_drop_bonus";request["requestId"]=id;
        const auto reply=authority.apply(Node{"maintenance",id,Role::Supervisor},request,[&](const Json& current){
            for(const auto& row:current["accounts"].members())if(row.second["phase"].asString()!="offline")return Json();
            Json plan=Json::object();plan["op"]="coordinated_patch";plan["accounts"]=Json::object();
            for(const char* table:{"users","sessions","globals"})plan["patch"][table]=Json::object();
            int count=0;
            for(const auto& row:current["accounts"].members()) {
                auto bonus=row.second["record"]["dailyDropBonus"];
                if(!pauseDropBonusForMaintenance(bonus,stoppedAt,id))continue;
                auto account=row.second;
                if(account["revision"].asDouble()+1>=9007199254740000.0)return Json();
                account["record"]["dailyDropBonus"]=std::move(bonus);
                account["revision"]=account["revision"].asDouble()+1;
                plan["accounts"][row.first]=std::move(account);++count;
            }
            plan["reply"]["paused"]=count;return plan;
        });
        std::cout<<reply.dump()<<std::endl;return reply["ok"].asBool()?0:1;
    }
    CouncilProjection council;
    bool projected=false;
    authority.inspect([&](const Json& current){projected=council.initialize(current,error);return Json();});
    if(!projected){std::cerr<<"council projection initialization failed\n";return 1;}
    const auto apply=[&](const Node& node,const Json& command,const Authority::Prepare& prepare=Authority::Prepare{}) {
        committedDelta=Json();auto reply=authority.apply(node,command,prepare);
        if(reply["ok"].asBool()&&!committedDelta.isNull()&&!council.committed(committedDelta)) {
            std::cerr<<"council projection out of sequence\n";std::exit(1);
        }
        return reply;
    };
    // Migrate once in the coordinator. A game loading the resulting snapshot
    // has no lease on its offline records and cannot rewrite them at startup.
    if(council.database().storedTable("councilLegacyActivity")["version"].asInt()!=1) {
        Json request=Json::object();request["op"]="initialize_legacy_activity";request["requestId"]="council-v1";
        const auto reply=apply(Node{"startup","migration",Role::Supervisor},request,[&](const Json& current){
            Json plan=Json::object();plan["op"]="global_patch";plan["writes"]=Json::array();
            plan["globalRevision"]=current["globalRevision"];
            plan["patch"]["globals"]["councilLegacyActivity"]=council::legacyActivitySnapshot(council.database());
            return plan;
        });
        if(!reply["ok"].asBool()) {
            std::cerr<<"coordinator activity migration failed\n";return 1;
        }
    }
    Presence presence;
    std::string line;
    while(std::getline(std::cin,line)) {
        Json output=Json::object();
        if(line.size()>16u*1024u*1024u){std::cout<<failure("request_too_large").dump()<<std::endl;continue;}
        const auto input=Json::parseOrNull(line);
        Node node{input["node"].asString(),input["boot"].asString(),
                  input["supervisor"].asBool()?Role::Supervisor:Role::Game};
        output["id"]=input["id"];
        if(!identifier(node.shard)||!identifier(node.boot))output["result"]=failure("bad_node");
        else if(input["request"]["op"].asString()=="snapshot") {
            if(!authority.healthy())output["result"]=failure("authority_frozen");
            else {
                auto reply=success();reply["worldCheckpointVersion"]=1;
                reply["state"]=authority.snapshot();output["result"]=std::move(reply);
            }
        } else if(input["request"]["op"].asString()=="presence") {
            const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto& request=input["request"];
            output["result"]=authority.inspect([&](const Json& current){return presence.exchange(current,node,request["rows"],now,request["notices"]);});
        } else if(input["request"]["op"].asString()=="public_view") {
            auto result=success();result["body"]=publicShardView(council.database(),input["request"]["input"]);output["result"]=result;
        } else if(input["request"]["op"].asString()=="hyper_status") {
            output["result"]=authority.inspect([&](const Json& current){return hyperStatus(current,node);});
        } else if(input["request"]["op"].asString().rfind("hyper_",0)==0) {
            const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            Json rejected;auto reply=apply(node,input["request"],[&](const Json& current){return planHyperBarrier(current,node,input["request"],now,rejected);});
            if(reply["error"].asString()=="transaction_preparation_rejected"&&rejected.isObject())reply=rejected;
            output["result"]=reply;
        } else if(input["request"]["op"].asString()=="council") {
            const auto& request=input["request"];const auto& body=request["input"];
            const auto action=body["action"].asString("view");const auto token=request["token"].asString();
            if(!authority.healthy())output["result"]=failure("authority_frozen");
            else if(!body.isObject()||token.size()>512||body.dump().size()>4096)output["result"]=failure("bad_transaction");
            else if(action=="view"||action=="player") {
                auto reply=success();reply["body"]=council.view(token,body);output["result"]=reply;
            } else {
                Json response;
                auto reply=apply(node,request,[&](const Json& current){return council.plan(current,token,body,response);});
                if(reply["ok"].asBool()) {
                    Json view=Json::object();view["action"]="view";reply["body"]=council.view(token,view);
                } else if(reply["error"].asString()=="transaction_preparation_rejected"&&!response["error"].isNull()) {
                    reply=success();reply["body"]=response;
                }
                output["result"]=reply;
            }
        } else output["result"]=apply(node,input["request"]);
        // Serialized with requests. Unknown compaction closes the host; never
        // continue appending after an uncertain snapshot/truncate boundary.
        if(store.compactDue()&&store.checkpoint(authority.snapshot())==Durability::Unknown) {
            std::cerr<<"coordinator compaction outcome unknown\n";return 1;
        }
        std::cout<<output.dump()<<std::endl;
    }
}
