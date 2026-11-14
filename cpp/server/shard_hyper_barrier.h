#pragma once
#include "server/shard_hyper.h"

namespace flix::shards {
// One bounded, durable H transaction. Leases never move between live games.
// Each holder saves and stops simulation before ready; after settlement it must
// install the new record before thaw. Timeouts abort costs, never steal leases.
inline Json hyperStatus(const Json& state,const Node& node) {
    Json out=success();const auto& tx=state["globals"]["hyperTransition"];
    out["operatorJob"]=state["globals"]["operatorHyperForgeRequest"];
    out["transaction"]=tx;out["accounts"]=Json::object();
    for(const auto& id:tx["participants"].items()) {
        const auto& a=state["accounts"][id.asString()];
        if(a["shard"].asString()==node.shard&&a["boot"].asString()==node.boot&&a["hyperLock"].isObject()) {
            auto row=a;
            if(tx["stage"].asString()=="freezing")row.erase("record");
            out["accounts"][id.asString()]=row;
        }
    }
    if(out["accounts"].size()&&tx["stage"].asString()!="freezing")out["hyperForges"]=state["globals"]["hyperForges"];
    return out;
}

inline Json planHyperBarrier(const Json& state,const Node& node,const Json& req,
                             std::int64_t now,Json& rejection) {
    auto reject=[&](const char* code){rejection=failure(code);return Json();};
    Json out=Json::object();out["op"]="coordinated_patch";out["accounts"]=Json::object();
    out["patch"]["globals"]=Json::object();out["reply"]=success();
    auto tx=state["globals"]["hyperTransition"];
    const auto action=req["op"].asString();
    if(action=="hyper_begin"||action=="hyper_operator_begin") {
        const bool operatorJob=action=="hyper_operator_begin";
        Json command=req;
        if(operatorJob) {
            const auto& job=state["globals"]["operatorHyperForgeRequest"];
            if(job["status"].asString()!="pending"||job["id"].asString()!=req["operatorRequest"].asString())return reject("operator_request_changed");
            command["account"]=job["userId"];command["operatorRequest"]=job["id"];
            command["action"]=job["mode"].asString()=="temporary15h"?"temporary_hammer":"forge";command["input"]="fragment";command["titan"]="";
        }
        if(tx.isObject()&&tx["stage"].asString()!="complete")return reject("hyper_busy");
        const auto id=command["account"].asString();const auto& a=state["accounts"][id];
        if(!identifier(id)||!a["record"].isObject()||(!operatorJob&&(a["phase"].asString()!="active"||a["shard"].asString()!=node.shard||a["boot"].asString()!=node.boot)))
            return reject("hyper_requester_not_owned");
        const auto mode=command["action"].asString();
        if(mode!="forge"&&mode!="abandon"&&mode!="temporary_hammer")return reject("bad_hyper_request");
        if(!operatorJob&&(mode=="temporary_hammer"||!req["operatorRequest"].asString().empty()))return reject("operator_only");
        tx=Json::object();tx["id"]=req["requestId"];tx["stage"]="freezing";
        tx["shard"]=node.shard;tx["boot"]=node.boot;tx["input"]=command;tx["operator"]=operatorJob;tx["deadline"]=double(now+10000);
        tx["participants"]=Json::array();tx["participants"].push(id);
        if(mode=="forge") {
            const auto owner=state["globals"]["hyperForges"][hyperForgeKey(hyperForgeOutput(command["input"].asString()),command["titan"].asString(),command["apexHammer"].asBool())]["owner"].asString();
            if(!owner.empty()&&owner!=id)tx["participants"].push(owner);
        }
        for(const auto& who:tx["participants"].items()) {
            auto row=state["accounts"][who.asString()];
            if(!row["record"].isObject()||row["phase"].asString()=="prepared"||row["hyperLock"].isObject())return reject("hyper_participant_busy");
            row["hyperLock"]["id"]=tx["id"];row["hyperLock"]["ready"]=row["phase"].asString()=="offline";
            out["accounts"][who.asString()]=row;
        }
    } else {
        if(!tx.isObject()||tx["id"].asString()!=req["transaction"].asString())return reject("hyper_transaction_changed");
        if(action=="hyper_ready"||action=="hyper_thaw") {
            const auto id=req["account"].asString();auto a=state["accounts"][id];
            if(a["hyperLock"]["id"].asString()!=tx["id"].asString()||a["phase"].asString()!="active"||
               a["shard"].asString()!=node.shard||a["boot"].asString()!=node.boot||
               !counter(req["epoch"])||!counter(req["revision"])||a["epoch"].asDouble()!=req["epoch"].asDouble()||a["revision"].asDouble()!=req["revision"].asDouble())
                return reject("hyper_fence_changed");
            if(action=="hyper_ready") {
                if(tx["stage"].asString()!="freezing")return reject("hyper_already_settled");
                a["hyperLock"]["ready"]=true;
            } else {
                if(tx["stage"].asString()!="settled")return reject("hyper_not_settled");
                a.erase("hyperLock");
            }
            out["accounts"][id]=a;
        } else if(action=="hyper_progress") {
            if(tx["stage"].asString()=="freezing") {
                bool ready=true;for(const auto& id:tx["participants"].items()) {
                    const auto& a=state["accounts"][id.asString()];
                    ready=ready&&(a["hyperLock"]["ready"].asBool()||a["phase"].asString()=="offline");
                }
                const auto& requester=state["accounts"][tx["input"]["account"].asString()];
                const bool alive=tx["operator"].asBool()||(requester["phase"].asString()=="active"&&requester["shard"].asString()==tx["shard"].asString()&&requester["boot"].asString()==tx["boot"].asString());
                const bool timeout=double(now)>=tx["deadline"].asDouble();
                if(!ready&&alive&&!timeout)return reject("hyper_waiting");
                Json result,planned;
                if(ready&&alive&&!timeout) {
                    // Only this trusted callback projects frozen participants
                    // under a virtual actor for the existing pure rule planner.
                    Json projection=Json::object();projection["globals"]["hyperForges"]=state["globals"]["hyperForges"];
                    auto input=tx["input"];Node actor{tx["shard"].asString(),tx["boot"].asString()};
                    for(const auto& id:tx["participants"].items()) {
                        auto a=state["accounts"][id.asString()];a["phase"]="active";a["shard"]=actor.shard;a["boot"]=actor.boot;
                        projection["accounts"][id.asString()]=a;
                        input["leases"][id.asString()]["epoch"]=a["epoch"];input["leases"][id.asString()]["revision"]=a["revision"];
                    }
                    planned=planHeldHyper(projection,actor,input,now,result);
                } else {result["ok"]=false;result["reason"]="跨服交接超时或玩家已离线，未扣除材料，请重试。";}
                if(result["ok"].asBool()) {
                    out["patch"]["globals"]["hyperForges"]=planned["patch"]["globals"]["hyperForges"];
                    for(const auto& write:planned["writes"].items()) {
                        auto a=state["accounts"][write["account"].asString()];a["record"]=write["record"];
                        if(a["revision"].asDouble()+1>=9007199254740000.0)return reject("counter_exhausted");a["revision"]=a["revision"].asDouble()+1;out["accounts"][write["account"].asString()]=a;
                    }
                }
                if(tx["operator"].asBool()) {
                    auto receipt=state["globals"]["operatorHyperForgeRequest"];
                    receipt["status"]=result["ok"].asBool()?"done":"failed";receipt["reason"]=result["reason"];
                    receipt["consumed"]=result["consumed"];receipt["completedAt"]=double(now);
                    out["patch"]["globals"]["operatorHyperForgeRequest"]=receipt;
                }
                tx["result"]=result;tx["stage"]="settled";
            }
        } else return reject("bad_hyper_operation");
        if(tx["stage"].asString()=="settled") {
            bool complete=true;
            for(const auto& id:tx["participants"].items()) {
                auto a=out["accounts"].contains(id.asString())?out["accounts"][id.asString()]:state["accounts"][id.asString()];
                if(a["phase"].asString()=="offline") {a.erase("hyperLock");out["accounts"][id.asString()]=a;}
                if(a["hyperLock"].isObject())complete=false;
            }
            if(complete)tx["stage"]="complete";
        }
    }
    out["patch"]["globals"]["hyperTransition"]=tx;out["reply"]["transaction"]=tx;
    return out;
}
} // namespace flix::shards
