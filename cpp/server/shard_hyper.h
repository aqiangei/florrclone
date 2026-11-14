#pragma once
#include "server/hyper_forge.h"
#include "server/shard_authority.h"

namespace flix::shards {
// Pure transaction planning, not a public RPC endpoint. The caller must first
// freeze every participant and collect their final records under one actor's
// leases. A foreign/offline/stale participant is never silently overwritten.
// Execute only via Authority::apply's trusted Prepare callback, so original
// request deduplication precedes all business rules and material consumption.
inline Json planHeldHyper(const Json& state,const Node& node,const Json& input,
                          std::int64_t now,Json& result) {
    const auto user=input["account"].asString(),action=input["action"].asString();
    const auto material=input["input"].asString(),titan=input["titan"].asString();
    const auto forgeKey=input["forgeKey"].asString();
    const bool apexHammer=input["apexHammer"].asBool();
    auto reject=[&](const char* reason){result=failure(reason);return Json();};
    if(!identifier(user)||material.size()>128||titan.size()>128||forgeKey.size()>257||now<0)
        return reject("bad_hyper_request");
    if(action!="forge"&&action!="abandon"&&action!="temporary_hammer")return reject("bad_hyper_request");
    const auto& forges=state["globals"]["hyperForges"];
    std::set<std::string> participants{user};
    if(action=="forge") {
        const auto owner=forges[hyperForgeKey(hyperForgeOutput(material),titan,apexHammer)]["owner"].asString();
        if(!owner.empty())participants.insert(owner);
    }
    Json seed=Json::object();seed["users"]=Json::object();seed["players"]=Json::object();
    seed["hyperForges"]=forges.isObject()?forges:Json::object();
    for(const auto& id:participants) {
        const auto& a=state["accounts"][id];const auto& fence=input["leases"][id];
        if(!a["record"].isObject()||a["phase"].asString()!="active"||
           a["shard"].asString()!=node.shard||a["boot"].asString()!=node.boot||
           !counter(fence["epoch"])||!counter(fence["revision"])||
           fence["epoch"].asDouble()!=a["epoch"].asDouble()||fence["revision"].asDouble()!=a["revision"].asDouble())
            return reject("hyper_participant_not_held");
        seed["players"][id]=a["record"];
    }
    Database scratch;std::string error;
    // This sink acknowledges only isolated memory planning. No network or file
    // writes occur until Authority atomically fsyncs the returned transaction.
    if(!scratch.loadAuthoritative(seed.dump(),[](const std::string&){return Database::CommitOutcome::Committed;},error))
        return reject("hyper_projection_invalid");
    HyperForgeResult business;
    if(action=="forge")business=forgeHyper(scratch,user,material,now,input["operatorRequest"].asString(),titan,apexHammer);
    else if(action=="abandon")business=abandonHyper(scratch,user,forgeKey);
    else {
        const auto request=input["operatorRequest"].asString();
        if(!identifier(request))return reject("bad_hyper_request");
        business=grantTemporaryHyperHammer(scratch,user,now,request);
    }
    result=Json::object();result["ok"]=business.ok;result["reason"]=business.reason;
    result["consumed"]=business.consumed;result["affected"]=Json::array();
    for(const auto& id:business.affected)result["affected"].push(id);
    if(!business.ok)return Json();
    const auto after=scratch.toJson();
    Json command=Json::object();command["op"]="global_patch";command["globalRevision"]=state["globalRevision"];
    command["writes"]=Json::array();command["patch"]["globals"]["hyperForges"]=after["hyperForges"];
    for(const auto& id:participants) {
        Json write=Json::object();write["account"]=id;write["record"]=after["players"][id];
        write["epoch"]=state["accounts"][id]["epoch"];write["revision"]=state["accounts"][id]["revision"];
        command["writes"].push(write);
    }
    return command;
}
} // namespace flix::shards
