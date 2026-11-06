#pragma once
#include "shared/core/json.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace flix {
struct ShardEndpoint {std::string id,label,host;std::uint16_t port=0;bool available=false;};
struct ShardDirectory {
    std::string current;std::vector<ShardEndpoint> choices;
    const ShardEndpoint* find(const std::string& id) const {
        for(const auto& row:choices)if(row.id==id)return &row;return nullptr;
    }
    bool parse(const Json& value,const std::string& initialHost,std::uint16_t initialPort) {
        ShardDirectory next;next.current=value["current"].asString();std::set<std::string> ids;
        if(!value["choices"].isArray()||value["choices"].size()!=2)return false;
        for(const auto& row:value["choices"].items()) {
            ShardEndpoint item;item.id=row["id"].asString();item.label=row["label"].asString();item.host=row["host"].asString();
            const int port=row["port"].asInt();
            if((item.id!="one"&&item.id!="two")||!ids.insert(item.id).second||item.label.empty()||item.label.size()>96||
                item.host.empty()||item.host.size()>253||port<1||port>65535||row["port"].asDouble()!=port||!row["available"].isBool())return false;
            for(const unsigned char c:item.host)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;
            item.port=static_cast<std::uint16_t>(port);item.available=row["available"].asBool();next.choices.push_back(item);
        }
        const auto* origin=next.find(next.current);
        if(!origin||origin->host!=initialHost||origin->port!=initialPort)return false;
        *this=std::move(next);return true;
    }
};
} // namespace flix
