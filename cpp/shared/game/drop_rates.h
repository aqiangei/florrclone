#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "shared/core/json.h"
#include "shared/game/rarity.h"

namespace flix {
struct DropRate { Rarity rarity=Rarity::Common; double probability=0; };
inline Rarity payoutDropRarity(Rarity rolled,Rarity mob) {
    if(mob==Rarity::Apex)
        return rarityIndex(rolled)>=rarityIndex(Rarity::Super)?Rarity::Unique:Rarity::Super;
    return rarityIndex(rolled)>=rarityIndex(Rarity::Apex)?Rarity::Unique:rolled;
}
using DropRates = std::vector<std::vector<DropRate>>;

inline bool parseDropRates(const Json& json, DropRates& out) {
    if(!json.isArray()||json.size()==0||json.size()>kLadderRarityCount)return false;
    out.clear();
    for(const Json& row:json.items()){
        if(!row.isArray())return false;
        std::vector<DropRate> rates;
        double cumulative=0;
        for(const Json& pair:row.items()){
            if(!pair.isArray()||pair.size()!=2||!pair[0].isNumber()||!pair[1].isNumber())return false;
            const double tier=pair[0].asDouble(),percent=pair[1].asDouble();
            if(!std::isfinite(tier)||tier!=std::floor(tier)||tier<0||tier>=kLadderRarityCount||
               !std::isfinite(percent)||percent<0||percent>100)return false;
            // Match reference Ha's integer CDF, including rounded tables
            // that total 100.1%. Never renormalize an omitted no-drop chance.
            const double weight=std::round(percent*1e8)/1e10;
            const double chance=std::max(0.0,std::min(1.0,cumulative+weight)-cumulative);
            rates.push_back({static_cast<Rarity>(static_cast<int>(tier)),chance});
            cumulative=std::min(1.0,cumulative+weight);
        }
        out.push_back(std::move(rates));
    }
    return true;
}
inline const std::vector<DropRate>& ratesFor(const DropRates& rates,Rarity mob) {
    static const std::vector<DropRate> none;
    // Hyper material drops promote Eternal's authored S tail to Eternal;
    // the old unused Hyper row has no such tail and is not the new policy.
    const auto tier=mob==Rarity::Apex?Rarity::Unique:mob;
    return rates.empty()?none:rates[std::min(rates.size()-1,static_cast<std::size_t>(rarityIndex(tier)))];
}
inline int rollDropRates(const DropRates& rates,Rarity mob,double sample) {
    if(sample<0)return -1;
    double sum=0;
    for(const auto& rate:ratesFor(rates,mob)){sum+=rate.probability;if(sample<sum)return rarityIndex(rate.rarity);}
    return -1;
}
} // namespace flix
