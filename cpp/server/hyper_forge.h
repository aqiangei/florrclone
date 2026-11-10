#pragma once
#include "server/db.h"
#include "server/hyper_history.h"
#include "server/timed_hammers.h"
#include "shared/game/npc.h"
#include "shared/game/constants.h"
#include <map>

namespace flix {
inline constexpr char kHyperAbandonedRefusal[] =
    "你已主动放弃这份超凡。其他玩家接手后，你才能再次争夺。";

struct HyperForgeState {
    std::string owner;
    int claims = 0;
    std::int64_t protectedUntil = 0;
    std::string abandonedBy;
};
inline std::string hyperForgeKey(const std::string& petal,const std::string& titan={}){return titan.empty()?petal:titan+":"+petal;}
inline HyperForgeState hyperForgeState(const Database& db,const std::string& petal,const std::string& titan={}) {
    const auto& row=db.storedTable("hyperForges")[hyperForgeKey(petal,titan)];
    return {row["owner"].asString(),std::max(0,row["claims"].asInt()),
            row["protectedUntil"].asInt64(),row["abandonedBy"].asString()};
}
struct HyperForgeResult {
    bool ok=false;int consumed=0;std::string reason;std::vector<std::string> affected;
};
inline int hyperHoldingCount(const Database& db,const std::string& userId){
    std::map<std::string,std::int64_t> physical,claims;
    if(const auto* record=db.findProgress(userId)){
        for(const auto& [key,value]:record->inventory["apex"].members())if(key.rfind("petal_",0)==0)physical[key.substr(6)]+=std::max(0,value.asInt());
        for(const auto& slot:record->loadout)if(slot&&slot->rarity==Rarity::Apex)++physical[slot->petalType];
    }
    for(const auto& [key,row]:db.storedTable("hyperForges").members())if(row["owner"].asString()==userId){
        const auto colon=key.find(':');++claims[colon==std::string::npos?key:key.substr(colon+1)];
    }
    for(const auto& [petal,count]:claims)physical[petal]=std::max(physical[petal],count);
    std::int64_t total=0;for(const auto& [petal,count]:physical)total+=count;
    return static_cast<int>(std::min<std::int64_t>(kMaxStackCount,total));
}
// Relinquish exactly one registered claim, with no material refund. Keep
// claim history and let another account claim the released copy first.
inline HyperForgeResult abandonHyper(Database& db,const std::string& userId,const std::string& forgeKey) {
    HyperForgeResult out;
    if(db.deferredCommits()){out.reason="跨服超凡事务暂未就绪，请稍后重试。";return out;}
    const auto oldTable=db.storedTable("hyperForges");
    const auto* original=db.findProgress(userId);
    if(!original || oldTable[forgeKey]["owner"].asString()!=userId){out.reason="你未持有这份超凡。";return out;}
    const auto colon=forgeKey.find(':');
    const auto petal=colon==std::string::npos?forgeKey:forgeKey.substr(colon+1);
    const auto key="petal_"+petal;
    const auto before=*original;
    auto& record=db.progress(userId);
    rememberHyperHoldings(record,static_cast<double>(db.nowMillis()));
    if(record.itemCount(Rarity::Apex,key)>0)record.addItem(Rarity::Apex,key,-1);
    else {
        bool removed=false;
        for(auto& slot:record.loadout)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal){slot.reset();removed=true;break;}
        if(!removed){out.reason="持有记录与库存不一致，请联系管理员。";return out;}
    }
    bool remaining=record.itemCount(Rarity::Apex,key)>0;
    for(const auto& slot:record.loadout)remaining=remaining||(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal);
    if(!remaining)for(auto& [name,slots]:record.loadoutPresets)
        for(auto& slot:slots)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal)slot.reset();
    auto& entry=db.rawTable("hyperForges")[forgeKey];
    entry["owner"]="";entry["protectedUntil"]=0;
    entry["abandonedBy"] = userId;
    db.markDirty();
    if(!db.save()){
        db.progress(userId)=before;db.rawTable("hyperForges")=oldTable;db.markDirty();
        out.reason="存档失败，未放弃超凡。";return out;
    }
    out.ok=true;out.affected.push_back(userId);
    out.reason="已放弃所选1份超凡，持有名额已释放。其他玩家接手后，你才能再次争夺。";
    return out;
}
inline HyperForgeResult grantTemporaryHyperHammer(Database& db,const std::string& userId,
                                                 std::int64_t now,const std::string& request) {
    HyperForgeResult out;const auto* original=db.findProgress(userId);
    if(db.deferredCommits()){out.reason="跨服超凡事务暂未就绪，请稍后重试。";return out;}
    if(!original){out.reason="账号不存在。";return out;}
    if(original->extra["temporaryHyperHammer"]["request"].asString()==request){out.ok=true;return out;}
    if(hyperHoldingCount(db,userId)>=kMaxHyperHoldings){out.reason="账号超凡持有已达5份上限，未新增。";return out;}
    if(original->itemCount(Rarity::Apex,"petal_mjolnir")>0){out.reason="账号已有超凡锤子，未重复补发。";return out;}
    for(const auto& slot:original->loadout)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType=="mjolnir"){
        out.reason="账号已装备超凡锤子，未重复补发。";return out;
    }
    const auto before=*original;auto& record=db.progress(userId);
    record.addItem(Rarity::Apex,"petal_mjolnir",1);
    Json lease=Json::object();lease["request"]=request;lease["active"]=true;lease["slot"]=-1;
    lease["expiresAt"]=Json(static_cast<double>(now+15LL*3600000));
    record.extra["temporaryHyperHammer"]=lease;db.markDirty();
    if(!db.save()){db.progress(userId)=before;db.markDirty();out.reason="存档失败，未补发。";return out;}
    out.ok=true;out.affected.push_back(userId);return out;
}
struct HyperForgePlan {
    HyperForgeResult result;
    std::map<std::string,PlayerRecord> players;
    std::string forgeKey;
    Json entry;
};

// Prepare an isolated transfer. Inventory is the bag, worn slots are
// accounted separately, and presets are references only.
inline HyperForgePlan planHyperForge(const Database& db,const std::string& userId,
                                   const std::string& input,std::int64_t now,
                                   const std::string& operatorRequest = {},const std::string& titan={}) {
    HyperForgePlan plan;
    auto& out=plan.result;
    if(db.deferredCommits()){out.reason="跨服超凡事务暂未就绪，请稍后重试。";return plan;}
    if(!hyperForgeEligible(input)){
        out.reason="超凡锤子只能使用10个永恒碎片，限时永恒锤不能用于争夺。";return plan;
    }
    const std::string petal=hyperForgeOutput(input);
    const auto forgeKey=hyperForgeKey(petal,titan);
    const auto state=hyperForgeState(db,petal,titan);
    if(hyperHoldingCount(db,userId)>=kMaxHyperHoldings){out.reason="账号超凡持有已达5份上限；已有物品保留，数量降低后才能继续锻造。";return plan;}
    const int cost=hyperForgeCost(state.claims,input);
    if(state.owner==userId){out.reason="你已经持有该超凡花瓣。";return plan;}
    if(state.abandonedBy==userId){
        out.reason=kHyperAbandonedRefusal;
        return plan;
    }
    if(state.protectedUntil>now){out.reason="保护期尚未结束，剩余"+
        std::to_string((state.protectedUntil-now+59999)/60000)+"分钟。";return plan;}
    const auto* original=db.findProgress(userId);const std::string key="petal_"+petal;
    const std::string material="petal_"+input;
    PlayerRecord available=original?*original:PlayerRecord{};
    if(input=="timed_mjolnir")expireHammerLeases(available,static_cast<double>(now));
    if(!original || available.itemCount(Rarity::Unique,material)<cost){
        out.reason="需要"+std::to_string(cost)+"个未装备的同种永恒材料。";return plan;}
    // Only this titan's registered owner loses ONE copy. Another titan's
    // copy (including one held by the same account) is independent.
    const auto* previous=state.owner.empty()?nullptr:db.findProgress(state.owner);
    const Json& oldTable=db.storedTable("hyperForges");
    const auto previousInput=oldTable[forgeKey]["input"].asString(
        petal=="mjolnir"?"fragment":petal);
    const std::string refundMaterial="petal_"+previousInput;
    const int refundPerCopy=hyperForgeRefund(std::max(1,oldTable[forgeKey]["paid"].asInt(5)));
    if(previous){
        auto& changed=plan.players.emplace(state.owner,*previous).first->second;int lost=0;
        rememberHyperHoldings(changed,static_cast<double>(now));
        if(changed.itemCount(Rarity::Apex,key)>0){changed.addItem(Rarity::Apex,key,-1);lost=1;}
        else for(auto& slot:changed.loadout)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal){slot.reset();lost=1;break;}
        bool remaining=changed.itemCount(Rarity::Apex,key)>0;
        for(const auto& slot:changed.loadout)remaining=remaining||(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal);
        if(!remaining)for(auto& [name,slots]:changed.loadoutPresets)
            for(auto& slot:slots)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal)slot.reset();
        const int refund=static_cast<int>(std::min<std::int64_t>(
            static_cast<std::int64_t>(lost)*refundPerCopy,kMaxStackCount));
        if(previousInput=="timed_mjolnir"){
            expireHammerLeases(changed,static_cast<double>(now));
            for(int n=0;n<refund;++n)addHammerLease(changed,static_cast<double>(now));
        }else changed.addItem(Rarity::Unique,refundMaterial,refund);
        out.affected.push_back(state.owner);
    }
    auto& winner=plan.players.emplace(userId,std::move(available)).first->second;
    winner.addItem(Rarity::Unique,material,-cost);winner.addItem(Rarity::Apex,key,1);
    if(input=="timed_mjolnir")expireHammerLeases(winner,static_cast<double>(now));
    Json entry=Json::object();entry["owner"]=userId;
    const int claims=std::min(1000000,state.claims+1);
    entry["claims"]=claims;entry["protectedUntil"]=Json(static_cast<double>(now+hyperProtectionMillis(claims)));
    entry["input"]=input;
    entry["paid"]=cost;
    if(!operatorRequest.empty())entry["operatorRequest"]=operatorRequest;
    plan.forgeKey=forgeKey;
    plan.entry=std::move(entry);
    out.affected.push_back(userId);out.consumed=cost;out.ok=true;
    return plan;
}

inline HyperForgeResult forgeHyper(Database& db,const std::string& userId,
                                   const std::string& input,std::int64_t now,
                                   const std::string& operatorRequest = {},const std::string& titan={}) {
    auto plan=planHyperForge(db,userId,input,now,operatorRequest,titan);
    auto& out=plan.result;
    if(!out.ok) return std::move(out);
    const Json oldTable=db.storedTable("hyperForges");
    std::map<std::string,PlayerRecord> before;
    for(auto& [id,record]:plan.players) {
        auto& live=db.progress(id);
        before.emplace(id,std::move(live));
        live=std::move(record);
    }
    db.rawTable("hyperForges")[plan.forgeKey]=std::move(plan.entry);
    db.markDirty();
    if(!db.save()){
        out.ok=false;
        out.consumed=0;
        if (db.backgroundSaveEnabled() && db.commitUncertain()) {
            out.affected.clear();
            out.reason = "存档结果未确认，服务器将停止处理请求。";
            return std::move(out);
        }
        for(const auto& [id,record]:before)db.progress(id)=record;
        db.rawTable("hyperForges")=oldTable.isObject()?oldTable:Json::object();db.markDirty();
        out.affected.clear();out.reason="存档写入失败，未完成锻造，请稍后重试。";return std::move(out);
    }
    return std::move(out);
}
}
