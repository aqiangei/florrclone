#pragma once
#include "server/shard_authority.h"
#include "shared/game/rarity.h"
#include <deque>
#include <map>
namespace flix::shards {
// Ephemeral presence, never part of inventory or the durable account journal.
class Presence {
    struct Entry { Json row; std::string shard,boot; double epoch=0,at=0; };
    std::map<std::string,Entry> entries_;
    struct Notice { std::string id, shard; Json event; double at; };
    std::deque<Notice> notices_;
public:
    Json exchange(const Json& state,const Node& node,const Json& rows,double now,const Json& notices={}) {
        if(node.role!=Role::Game||(node.shard!="one"&&node.shard!="two")||!rows.isArray()||rows.size()>1024)
            return failure("invalid_presence");
        for(auto it=entries_.begin();it!=entries_.end();)
            if(it->second.shard==node.shard&&it->second.boot==node.boot)it=entries_.erase(it);else ++it;
        for(const auto& row:rows.items()) {
            const auto id=row["account"].asString();const auto& a=state["accounts"][id];
            if(a["phase"].asString()!="active"||a["shard"].asString()!=node.shard||a["boot"].asString()!=node.boot)continue;
            Json safe=Json::object();
            for(const auto* key:{"name","guild","location"})safe[key]=row[key].asString().substr(0,128);
            safe["level"]=std::clamp(row["level"].asInt(),1,65535);safe["shard"]=node.shard;
            safe["account"]=id;
            entries_[id]={std::move(safe),node.shard,node.boot,a["epoch"].asDouble(),now};
        }
        Json result=success();result["rows"]=Json::array();
        for(auto it=entries_.begin();it!=entries_.end();) {
            const auto& e=it->second;const auto& a=state["accounts"][it->first];
            if(now-e.at>=8000||a["phase"].asString()!="active"||a["shard"].asString()!=e.shard||a["boot"].asString()!=e.boot||a["epoch"].asDouble()!=e.epoch)it=entries_.erase(it);
            else {result["rows"].push(e.row);++it;}
        }
        while(!notices_.empty()&&now-notices_.front().at>=30000)notices_.pop_front();
        if(notices.isArray()&&notices.size()<=64)for(const auto& notice:notices.items()) {
            const auto nonce=notice["id"].asString();
            const int mob=notice["mob"].asInt(-1),tier=notice["rarity"].asInt(-1);
            if(nonce.size()!=32||nonce.find_first_not_of("0123456789abcdef")!=std::string::npos||
               mob<0||mob>=65535||tier<0||tier>=kRarityCount)continue;
            const auto id=node.shard+"/"+node.boot+"/"+nonce;
            if(std::any_of(notices_.begin(),notices_.end(),[&](const Notice& n){return n.id==id;}))continue;
            Json event=Json::object();
            event["id"]=id;event["mob"]=mob;event["rarity"]=tier;event["summoned"]=notice["summoned"].asBool();
            notices_.push_back({id,node.shard,std::move(event),now});
            if(notices_.size()>256)notices_.pop_front();
        }
        result["notices"]=Json::array();
        for(const auto& notice:notices_)if(notice.shard!=node.shard)result["notices"].push(notice.event);
        return result;
    }
};
}
