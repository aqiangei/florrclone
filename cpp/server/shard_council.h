#pragma once
#include "server/council.h"
#include "server/shard_authority.h"

namespace flix::shards {
// Incremental, read-only database projection; the authority owns all writes.
class CouncilProjection {
public:
    bool initialize(const Json& state,std::string& error) {
        globals_=state["globals"];sequence_=state["sequence"].asDouble();
        Json root=globals_;root.erase("shardWorlds");root["players"]=Json::object();
        for(const auto& row:state["accounts"].members())root["players"][row.first]=row.second["record"];
        return db_.loadAuthoritative(root.dump(),[](const std::string&){return Database::CommitOutcome::Rejected;},error);
    }
    bool committed(const Json& delta) {
        if(delta["sequence"].asDouble()!=sequence_+1)return false;
        Json update=Json::object();update["players"]=Json::object();update["globals"]=Json::object();
        for(const auto& row:delta["accounts"].members())update["players"][row.first]=row.second["record"];
        if(delta["globalPatch"].isObject()) {
            const auto& patch=delta["globalPatch"];
            for(const auto* table:{"users","sessions"}) {
                update[table]=patch[table];
                for(const auto& row:patch[table].members()) {
                    if(row.second.isNull())globals_[table].erase(row.first);else globals_[table][row.first]=row.second;
                }
            }
            for(const auto& row:patch["globals"].members()) {
                if(row.first!="shardWorlds")update["globals"][row.first]=row.second;
                if(row.second.isNull())globals_.erase(row.first);else globals_[row.first]=row.second;
            }
        }
        if(delta["globals"].isObject()) {
            const auto& next=delta["globals"];
            for(const auto* table:{"users","sessions"}) {
                update[table]=Json::object();
                for(const auto& row:globals_[table].members())if(next[table][row.first].isNull())update[table][row.first]=Json();
                for(const auto& row:next[table].members())
                    if(row.second.dump()!=globals_[table][row.first].dump())update[table][row.first]=row.second;
            }
            for(const auto& row:globals_.members())
                if(row.first!="users"&&row.first!="sessions"&&row.first!="shardWorlds"&&next[row.first].isNull())update["globals"][row.first]=Json();
            for(const auto& row:next.members())
                if(row.first!="users"&&row.first!="sessions"&&row.first!="shardWorlds"&&row.second.dump()!=globals_[row.first].dump())
                    update["globals"][row.first]=row.second;
            globals_=next;
        }
        if(!db_.applyAuthoritativeDelta(update))return false;
        sequence_=delta["sequence"].asDouble();return true;
    }
    const Database& database() const {return db_;}
    Json view(const std::string& token,const Json& input) {return council::request(db_,token,input);}
    Json plan(const Json& state,const std::string& token,const Json& input,Json& body) {
        Json command;
        body=council::request(db_,token,input,[&](const Json& polls){
            command=Json::object();command["op"]="global_patch";command["writes"]=Json::array();
            command["globalRevision"]=state["globalRevision"];
            command["patch"]["globals"]["councilPolls"]=polls;return true;
        });
        return command;
    }
private:
    Database db_;Json globals_;double sequence_=0;
};
} // namespace flix::shards
