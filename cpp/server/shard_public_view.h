#pragma once
#include "server/hyper_board.h"
#include "server/hyper_forge.h"
#include "server/playtime_board.h"
#include "server/practice_board.h"
namespace flix::shards {
inline Json publicShardView(const Database& db,const Json& input) {
    Json result=Json::object();const auto action=input["kind"].asString();
    if(action=="titan") {
        const auto material=input["input"].asString();
        const auto state=hyperForgeState(db,hyperForgeOutput(material),input["titan"].asString());
        const auto* owner=db.findUserById(state.owner);
        result["name"]=owner?owner->username:"";
        result["remaining"]=double(std::max<std::int64_t>(0,state.protectedUntil-db.nowMillis()));
        result["cost"]=hyperForgeCost(state.claims,material);
        result["held"]=hyperHoldingCount(db,input["account"].asString());
    } else if (action == "practice") {
        auto board = practiceBoardView(db, input["account"].asString(), input["includeAdmins"].asBool());
        board["own"] = Json::array();
        return board;
    } else if(action=="leaderboard") {
        const auto* self = db.findUserById(input["account"].asString());
        const auto playtimes = playtimeBoard(db, input["includeAdmins"].asBool(), db.userCount());
        result["playtime"] = Json::array();
        for (std::size_t i = 0; i < playtimes.size(); ++i) {
            const auto& entry = playtimes[i];
            if (self && self->username == entry.name) result["playtimeRank"] = double(i + 1);
            if (i < 50) {
                Json row = Json::object(); row["name"] = entry.name; row["seconds"] = entry.seconds;
                result["playtime"].push(row);
            }
        }
        std::vector<std::pair<std::string,double>> rows;unsigned active=0;
        for(const auto& name:db.usernames()) {
            const auto* user=db.findUser(name);if(!user)continue;
            if(user->lastActiveAtMillis>=db.nowMillis()-24*60*60*1000LL)++active;
            if(user->admin&&!input["includeAdmins"].asBool())continue;
            const auto* record=db.findProgress(user->id);rows.emplace_back(name,record?record->totalXp:0.);
        }
        const auto shown=std::min<std::size_t>(50,rows.size());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        if (self) {
            const auto match = std::find_if(rows.begin(), rows.end(),
                [&](const auto& row) { return row.first == self->username; });
            if (match != rows.end()) result["levelRank"] = double(match - rows.begin() + 1);
        }
        result["rows"]=Json::array();result["users"]=double(db.userCount());result["active"]=double(input["admin"].asBool()?active:0);
        for(std::size_t i=0;i<shown;++i){Json row=Json::object();row["name"]=rows[i].first;row["xp"]=rows[i].second;row["level"]=levelFromTotalXp(rows[i].second).level;result["rows"].push(row);}
        const auto holdings=hyperHoldingBoard(db,db.nowMillis());const auto total=static_cast<unsigned>(holdings.size());
        if (self) result["hyperRank"] = double(hyperHoldingOwnerRank(holdings, self->username));
        const auto requested=static_cast<unsigned>(std::max(0.,input["offset"].asDouble()));
        const auto offset=total?std::min(requested/kHyperBoardPageSize,(total-1)/kHyperBoardPageSize)*kHyperBoardPageSize:0;
        result["total"]=double(total);result["offset"]=double(offset);result["holdings"]=Json::array();
        for(auto i=offset;i<std::min(total,offset+kHyperBoardPageSize);++i){
            const auto& value=holdings[i];Json row=Json::object();row["name"]=value.name;row["petal"]=value.petal;row["count"]=double(value.count);row["ownerTotal"]=double(value.ownerTotal);result["holdings"].push(row);
        }
    }
    return result;
}
} // namespace flix::shards
