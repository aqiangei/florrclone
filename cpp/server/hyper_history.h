#pragma once
#include "server/db.h"
#include <map>

namespace flix {
inline std::map<std::string, std::int64_t> currentHyperHoldings(const PlayerRecord& p, double now) {
    std::map<std::string, std::int64_t> counts;
    for(const auto& [key,value]:p.inventory["apex"].members())
        if(key.rfind("petal_",0)==0) counts[key.substr(6)]+=PlayerRecord::stackCount(value);
    for(const auto& slot:p.loadout) if(slot&&slot->rarity==Rarity::Apex) ++counts[slot->petalType];
    const auto& lease=p.extra["temporaryHyperHammer"];
    if(lease["active"].asBool()&&lease["expiresAt"].asDouble()<=now&&counts["mjolnir"]>0) --counts["mjolnir"];
    return counts;
}
// Observation times, never an invented acquisition date. Presets are references.
inline void rememberHyperHoldings(PlayerRecord& p, double now) {
    for(const auto& [petal,count]:currentHyperHoldings(p,now)) if(count>0) {
        auto& row=p.extra["hyperHistory"][petal];
        if(row["firstSeenAt"].asDouble()<=0) row["firstSeenAt"]=now;
        row["lastSeenAt"]=std::max(now,row["lastSeenAt"].asDouble());
    }
}
}
