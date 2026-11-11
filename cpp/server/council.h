#pragma once
#include "server/db.h"
#include "server/hyper_history.h"
#include "shared/game/constants.h"
#include <algorithm>
#include <cmath>

namespace flix::council {
inline constexpr std::size_t kProposalRankLimit = 20;
inline Json legacyActivitySnapshot(const Database& db) {
    Json table=Json::object();table["version"]=1;table["at"]=static_cast<double>(db.nowMillis());table["accounts"]=Json::object();
    for(const auto& id:db.playerIds()) {
        const auto* a=db.findUserById(id);const auto* p=db.findProgress(id);
        if(!a||a->admin||a->guest||!p) continue;
        const int level=levelFromTotalXp(p->totalXp).level;
        Json row=Json::object();row["level"]=level;
        row["seconds"]=9.0*(level-1.0)*(level-1.0);table["accounts"][id]=row;
    }
    return table;
}
inline bool initializeLegacyActivity(Database& db) {
    if(db.storedTable("councilLegacyActivity")["version"].asInt()==1) return true;
    const Json previous=db.storedTable("councilLegacyActivity");
    db.rawTable("councilLegacyActivity")=legacyActivitySnapshot(db);db.markDirty();
    if(db.save()) return true;
    db.rawTable("councilLegacyActivity")=previous.isObject()?previous:Json::object();db.markDirty();return false;
}
inline Json weightInputs(const Database& db) {
    Json rows=Json::array();
    for(const auto& id:db.playerIds()) {
        const auto* a=db.findUserById(id); const auto* p=db.findProgress(id);
        if(!a||a->guest||a->admin||!p) continue;
        const double seconds=std::max(0.0,db.storedTable("councilActivity")[id].asDouble())+
            std::max(0.0,p->extra["councilActiveSeconds"].asDouble());
        int level=levelFromTotalXp(p->totalXp).level;
        if(level<2 && seconds<60) continue;
        double petals=0;
        for(int r=0;r<rarityIndex(Rarity::Admin);++r) {
            double count=0;
            for(const auto& kv:p->inventory[rarityName(static_cast<Rarity>(r))].members())
                if(kv.first.rfind("petal_",0)==0) count+=PlayerRecord::stackCount(kv.second);
            for(const auto& item:p->loadout) if(item && rarityIndex(item->rarity)==r) ++count;
            petals+=(r+1)*std::log1p(count);
        }
        Json row=Json::object(); row["id"]=id; row["name"]=a->username;
        row["level"]=level; row["petals"]=petals;
        row["recordedSeconds"]=seconds;
        row["levelScore"]=std::max(0.0,level-1.0);
        rows.push(row);
    }
    return rows;
}

// Percentages use integer millionths: the published allocation sums exactly to 100%.
inline Json weights(const Database& db) {
    Json rows=weightInputs(db);
    double totals[3]={};
    for (auto& row : rows.items()) {
        const double seconds = row["recordedSeconds"].asDouble();
        const double timeScore = std::log1p(seconds / 3600);
        row["seconds"] = seconds;
        row["timeScore"] = timeScore;
        totals[0] += timeScore;
        totals[1] += row["levelScore"].asDouble();
        totals[2] += row["petals"].asDouble();
    }
    // Equal level/activity budgets; activity is shrunk toward level shares at cold start.
    // This is a share prior only: it never modifies recorded or credited seconds.
    const double petalWeight=totals[2]>0?.10:0;
    const double activityWeight=(1-petalWeight)/2;
    const double prior=rows.size()*std::log1p(10.0);
    std::vector<std::pair<double,std::size_t>> remainders; int allocated=0;
    for(std::size_t i=0;i<rows.size();++i) {
        const double levelShare=totals[1]>0?rows[i]["levelScore"].asDouble()/totals[1]:1.0/rows.size();
        const double petalShare=totals[2]>0?rows[i]["petals"].asDouble()/totals[2]:0;
        const double timeShare=(rows[i]["timeScore"].asDouble()+prior*levelShare)/(totals[0]+prior);
        rows[i]["activityWeight"]=activityWeight;
        rows[i]["levelContribution"]=activityWeight*levelShare;
        rows[i]["petalContribution"]=petalWeight*petalShare;
        rows[i]["activityContribution"]=activityWeight*timeShare;
        const double score=activityWeight*(levelShare+timeShare)+petalWeight*petalShare;
        double exact=score*1000000; int units=static_cast<int>(std::floor(exact));
        rows[i]["units"]=units; allocated+=units; remainders.emplace_back(exact-units,i);
    }
    std::stable_sort(remainders.begin(),remainders.end(),[](auto a,auto b){return a.first>b.first;});
    for(int i=0;rows.size()>0 && i<1000000-allocated;++i) {
        auto index=remainders[static_cast<std::size_t>(i)%remainders.size()].second;
        rows[index]["units"]=rows[index]["units"].asInt()+1;
    }
    std::vector<std::size_t> order;for(std::size_t i=0;i<rows.size();++i)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){
        const int left=rows[a]["units"].asInt(),right=rows[b]["units"].asInt();
        return left!=right?left>right:rows[a]["id"].asString()<rows[b]["id"].asString();
    });
    Json ranked=Json::array();for(auto index:order){rows[index]["rank"]=static_cast<int>(ranked.size()+1);ranked.push(rows[index]);}
    return ranked;
}

inline bool canClosePolls(const Database& db,const Account* account) {
    if(!account || account->guest || account->adminBanned) return false;
    // Council-only delegation is persisted by immutable account ID. It grants
    // no console/admin powers and does not remove the player's voting weight.
    return account->adminActive() || db.storedTable("councilManagers")[account->id].asBool();
}

inline Json request(Database& db,const std::string& token,const Json& input,
                    const std::function<bool(const Json&)>& commitPolls={}) {
    auto error=[](const char* message){Json out=Json::object();out["error"]=message;return out;};
    const std::string action=input["action"].asString("view");
    if (action == "ranking-data") {
        Json out = Json::object();
        out["players"] = weightInputs(db);
        return out;
    }
    const auto* session=db.resolveSession(token,false);
    const auto* account=session?db.findUserById(session->userId):nullptr;
    if (action == "identity") {
        Json out = Json::object();
        out["me"] = account && !account->guest ? account->id : "";
        out["accountName"] = account && !account->guest ? account->username : "";
        out["canClose"] = canClosePolls(db, account);
        return out;
    }
    if(action=="player") {
        const auto id=input["id"].asString();const auto* a=db.findUserById(id);const auto* p=db.findProgress(id);
        if(!a||a->admin||a->guest||!p) return error("该玩家暂无公开详情");
        Json out=Json::object();out["id"]=id;out["name"]=a->username;out["current"]=Json::array();out["history"]=Json::array();
        const auto current=currentHyperHoldings(*p,static_cast<double>(db.nowMillis()));
        for(const auto& [petal,count]:current) if(count>0) {Json row=Json::object();row["petal"]=petal;row["count"]=static_cast<double>(count);out["current"].push(row);}
        for(const auto& [petal,record]:p->extra["hyperHistory"].members()) {
            Json row=Json::object();row["petal"]=petal;row["firstSeenAt"]=record["firstSeenAt"];row["lastSeenAt"]=record["lastSeenAt"];
            auto held=current.find(petal);row["holding"]=held!=current.end()&&held->second>0;out["history"].push(row);
        }
        out["coverage"]=db.storedTable("councilHistoryCoverage");return out;
    }
    if(action!="view") {
        if(!account||account->guest) return error("请先在游戏首页登录正式账号");
        Json original=db.storedTable("councilPolls");
        Json polls=original.isObject()?original:Json::object();
        if(action=="create") {
            const Json snapshot=weights(db);
            bool eligible=false;
            for(std::size_t i=0;i<std::min(kProposalRankLimit,snapshot.size());++i)
                if(snapshot[i]["id"].asString()==account->id && snapshot[i]["units"].asInt()>0) eligible=true;
            if(!eligible) return error("仅当前综合权重排行榜前20名可以发布议题");
            int active=0;
            for(const auto& entry:polls.members()) if(entry.second["authorId"].asString()==account->id) {
                if(db.nowMillis()-entry.second["createdAt"].asDouble()<86400000.0) return error("每个账号每24小时最多发布1个议题");
                if(entry.second["endsAt"].asDouble()>db.nowMillis())++active;
            }
            if(active>=3) return error("每个账号最多同时有3个进行中的议题");
            const auto title=input["title"].asString();
            if(title.find_first_not_of(" \t\r\n")==std::string::npos||title.size()>240||polls.size()>=200) return error("标题为空、过长或议题数量已达上限");
            Json poll=Json::object(); poll["title"]=title;
            poll["authorId"]=account->id;poll["authorName"]=account->username;poll["createdAt"]=static_cast<double>(db.nowMillis());
            poll["endsAt"]=static_cast<double>(db.nowMillis())+86400000.0;
            poll["weights"]=snapshot; poll["votes"]=Json::object();poll["weightRule"]=3;
            if(poll["weights"].size()==0) return error("暂无符合条件的玩家");
            polls[std::to_string(db.nowMillis())+"-"+std::to_string(polls.size())]=poll;
        } else if(action=="close") {
            if(!canClosePolls(db,account)) return error("仅议会管理账号可以提前结束议题");
            const auto id=input["id"].asString();
            if(!polls[id].isObject()) return error("议题不存在");
            auto& poll=polls[id];
            if(db.nowMillis()<poll["endsAt"].asDouble()) {
                poll["endsAt"]=static_cast<double>(db.nowMillis());
                poll["closedBy"]=account->id;
            }
        } else if(action=="vote") {
            const auto id=input["id"].asString();
            if(!polls[id].isObject()) return error("议题不存在");
            auto& poll=polls[id];
            if(db.nowMillis()>=poll["endsAt"].asDouble()) return error("投票已结束");
            bool eligible=false; const auto& snapshot=poll["weights"];
            for(std::size_t i=0;i<snapshot.size();++i) if(snapshot[i]["id"].asString()==account->id&&snapshot[i]["units"].asInt()>0) eligible=true;
            if(!eligible) return error("本议题开始时你的账号没有投票权重");
            const auto choice=input["choice"].asString();
            if(choice!="yes"&&choice!="no"&&choice!="abstain") return error("无效选项");
            poll["votes"][account->id]=choice;
        } else return error("未知操作");
        if(commitPolls) {
            if(!commitPolls(polls))return error("保存尚未确认，请稍后查询议题状态");
        } else {
            db.rawTable("councilPolls")=polls; db.markDirty();
            if(!db.save()) {db.rawTable("councilPolls")=original.isObject()?original:Json::object();db.markDirty();return error("保存失败，操作未生效，请稍后重试");}
        }
    }
    Json out=Json::object(); out["players"]=weights(db); out["me"]=account?account->id:"";
    out["activityWeight"]=out["players"].size()?out["players"][0]["activityWeight"]:Json(0);
    out["canPublish"]=false;
    for(std::size_t i=0;i<std::min(kProposalRankLimit,out["players"].size());++i)
        if(account && !account->guest && out["players"][i]["id"].asString()==account->id && out["players"][i]["units"].asInt()>0) out["canPublish"]=true;
    out["accountName"]=account?account->username:"";
    out["admin"]=account&&account->adminActive();
    out["canClose"]=canClosePolls(db,account); out["polls"]=Json::array();
    for(const auto& entry:db.storedTable("councilPolls").members()) {
        const auto& p=entry.second; Json row=Json::object(); row["id"]=entry.first;row["title"]=p["title"];row["authorName"]=p["authorName"];
        row["createdAt"]=p["createdAt"];row["endsAt"]=p["endsAt"];row["closed"]=db.nowMillis()>=p["endsAt"].asDouble();
        row["legacyWeights"]=p["weightRule"].asInt()<3;
        int yes=0,no=0,abstain=0,count=0; const auto& snapshot=p["weights"];
        for(std::size_t i=0;i<snapshot.size();++i) {
            const auto id=snapshot[i]["id"].asString(); const auto vote=p["votes"][id].asString();int units=snapshot[i]["units"].asInt();
            if(vote=="yes")yes+=units;else if(vote=="no")no+=units;else if(vote=="abstain")abstain+=units;else continue;
            ++count;
        }
        row["result"]=yes>no?"passed":no>yes?"rejected":"tied";
        row["yes"]=yes;row["no"]=no;row["abstain"]=abstain;row["count"]=count;
        row["mine"]=account?p["votes"][account->id].asString():"";out["polls"].push(row);
    }
    return out;
}
}
