#pragma once
#include <algorithm>
#include <vector>
#include "server/db.h"
#include "shared/game/npc.h"

namespace flix {
// Each lease represents one physical hammer; a slot association survives
// saves, so an older equipped hammer expires without deleting a newer copy.
inline int expireHammerLeases(PlayerRecord& record, double now) {
    struct Lease { double end; int slot; };
    std::vector<Lease> leases;
    const auto worn = [&](int slot) {
        return slot >= 0 && slot < static_cast<int>(record.loadout.size()) &&
            record.loadout[slot] && record.loadout[slot]->petalType == "timed_mjolnir" &&
            record.loadout[slot]->rarity == kTitanOutputRarity;
    };
    auto& stored = record.extra["titanHammerLeases"];
    if (stored.isArray()) {
        for (std::size_t i=0;i<stored.size();++i) {
            const double end=stored[i]["expiresAt"].asDouble(0);
            if (end>0) leases.push_back({end,stored[i]["slot"].asInt(-1)});
        }
    } else {
        const double old=record.extra["titanHammerExpiresAt"].asDouble(0);
        if (old>0) {
            for(int slot=0;slot<static_cast<int>(record.loadout.size());++slot)
                if(worn(slot)) leases.push_back({old,slot});
            for(int n=0;n<record.itemCount(kTitanOutputRarity,"petal_timed_mjolnir");++n)
                leases.push_back({old,-1});
        }
    }
    std::vector<bool> assigned(record.loadout.size(),false);
    for(auto& lease:leases) {
        if(!worn(lease.slot) || assigned[lease.slot]) lease.slot=-1;
        else assigned[lease.slot]=true;
    }
    std::stable_sort(leases.begin(),leases.end(),[](const Lease& a,const Lease& b){return a.end<b.end;});
    for(int slot=0;slot<static_cast<int>(record.loadout.size());++slot) {
        if(!worn(slot)||assigned[slot])continue;
        auto free=std::find_if(leases.begin(),leases.end(),[](const Lease& lease){return lease.slot<0;});
        if(free!=leases.end()){free->slot=slot;assigned[slot]=true;}
    }
    Json next=Json::array();int expired=0;
    for(const auto& lease:leases) {
        if(lease.end<=now) {
            if(worn(lease.slot))record.loadout[lease.slot].reset();
            else if(record.itemCount(kTitanOutputRarity,"petal_timed_mjolnir")>0)
                record.addItem(kTitanOutputRarity,"petal_timed_mjolnir",-1);
            ++expired;continue;
        }
        Json item=Json::object();item["expiresAt"]=Json(lease.end);item["slot"]=lease.slot;next.push(std::move(item));
    }
    stored=std::move(next);record.extra["titanHammerExpiresAt"]=Json(0);
    return expired;
}
inline void addHammerLease(PlayerRecord& record,double now) {
    if (!record.extra["titanHammerLeases"].isArray()) expireHammerLeases(record,now);
    Json lease=Json::object();lease["expiresAt"]=Json(now+3600000);lease["slot"]=-1;
    record.extra["titanHammerLeases"].push(std::move(lease));
    record.addItem(kTitanOutputRarity,"petal_timed_mjolnir",1);
}
} // namespace flix
