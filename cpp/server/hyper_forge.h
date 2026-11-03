#pragma once
#include "server/db.h"
#include "server/timed_hammers.h"
#include "shared/game/npc.h"
#include <map>

namespace flix {
struct HyperForgeState {std::string owner;int claims=0;std::int64_t protectedUntil=0;};
inline HyperForgeState hyperForgeState(const Database& db,const std::string& petal) {
    const auto& row=db.storedTable("hyperForges")[petal];
    return {row["owner"].asString(),std::max(0,row["claims"].asInt()),
            row["protectedUntil"].asInt64()};
}
struct HyperForgeResult {
    bool ok=false;std::string reason;std::vector<std::string> affected;
};
inline HyperForgeResult grantTemporaryHyperHammer(Database& db,const std::string& userId,
                                                 std::int64_t now,const std::string& request) {
    HyperForgeResult out;const auto* original=db.findProgress(userId);
    if(!original){out.reason="账号不存在。";return out;}
    if(original->extra["temporaryHyperHammer"]["request"].asString()==request){out.ok=true;return out;}
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
// Caller first persists all live records. Inventory is the BAG internally;
// worn slots are accounted separately, while presets are references only.
inline HyperForgeResult forgeHyper(Database& db,const std::string& userId,
                                   const std::string& input,std::int64_t now,
                                   const std::string& operatorRequest = {}) {
    HyperForgeResult out;
    if(!hyperForgeEligible(input)){
        out.reason="超凡锤子需要5把未装备的永恒锤子，不能使用碎片。";return out;
    }
    const std::string petal=hyperForgeOutput(input);
    const auto state=hyperForgeState(db,petal);
    if(state.owner==userId){out.reason="你已经持有该超凡花瓣。";return out;}
    if(state.protectedUntil>now){out.reason="保护期尚未结束，剩余"+
        std::to_string((state.protectedUntil-now+59999)/60000)+"分钟。";return out;}
    const auto* original=db.findProgress(userId);const std::string key="petal_"+petal;
    const std::string material="petal_"+input;
    PlayerRecord available=original?*original:PlayerRecord{};
    if(input=="timed_mjolnir")expireHammerLeases(available,static_cast<double>(now));
    if(!original || available.itemCount(Rarity::Unique,material)<kHyperForgeCost){
        out.reason="需要5个未装备的同种永恒花瓣。";return out;}
    std::map<std::string,PlayerRecord> before;before[userId]=*original;
    for(const auto& id:db.playerIds()){
        const auto* record=db.findProgress(id);if(!record)continue;
        if(petal=="mjolnir"&&id!=userId&&id!=state.owner&&record->extra["temporaryHyperHammer"]["active"].asBool(false))continue;
        int count=record->itemCount(Rarity::Apex,key);
        for(const auto& slot:record->loadout)
            if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal)++count;
        if(!count)continue;
        if(id==userId){out.reason="你已经持有该超凡花瓣。";return out;}
        before[id]=*record;
    }
    const Json oldTable=db.storedTable("hyperForges");
    const auto previousInput=oldTable[petal]["input"].asString(
        petal=="mjolnir"?"fragment":petal);
    const std::string refundMaterial="petal_"+previousInput;
    for(const auto& [id,record]:before){
        if(id==userId)continue;
        auto& changed=db.progress(id);int lost=changed.itemCount(Rarity::Apex,key);
        changed.addItem(Rarity::Apex,key,-lost);
        for(auto& slot:changed.loadout)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal){slot.reset();++lost;}
        for(auto& [name,slots]:changed.loadoutPresets)
            for(auto& slot:slots)if(slot&&slot->rarity==Rarity::Apex&&slot->petalType==petal)slot.reset();
        const int refund=static_cast<int>(std::min<std::int64_t>(
            static_cast<std::int64_t>(lost)*kHyperForgeRefund,kMaxStackCount));
        if(previousInput=="timed_mjolnir"){
            expireHammerLeases(changed,static_cast<double>(now));
            for(int n=0;n<refund;++n)addHammerLease(changed,static_cast<double>(now));
        }else changed.addItem(Rarity::Unique,refundMaterial,refund);
        out.affected.push_back(id);
    }
    auto& winner=db.progress(userId);
    if(input=="timed_mjolnir")winner=available;
    winner.addItem(Rarity::Unique,material,-kHyperForgeCost);winner.addItem(Rarity::Apex,key,1);
    if(input=="timed_mjolnir")expireHammerLeases(winner,static_cast<double>(now));
    Json entry=Json::object();entry["owner"]=userId;
    const int claims=std::min(1000000,state.claims+1);
    entry["claims"]=claims;entry["protectedUntil"]=Json(static_cast<double>(now+hyperProtectionMillis(claims)));
    entry["input"]=input;
    if(!operatorRequest.empty())entry["operatorRequest"]=operatorRequest;
    db.rawTable("hyperForges")[petal]=entry;db.markDirty();
    if(!db.save()){
        for(const auto& [id,record]:before)db.progress(id)=record;
        db.rawTable("hyperForges")=oldTable.isObject()?oldTable:Json::object();db.markDirty();
        out.affected.clear();out.reason="存档写入失败，未完成锻造，请稍后重试。";return out;
    }
    out.affected.push_back(userId);out.ok=true;return out;
}
}
