#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "shared/core/json.h"
#include "shared/game/rarity.h"

namespace flix {

inline constexpr double kApexPetalDropChance = 0.02;
struct DropRate { Rarity rarity=Rarity::Common; double probability=0; };
inline Rarity payoutDropRarity(Rarity rolled,Rarity mob) {
    // A Hyper kill must not promote every Super roll into an Eternal.
    // Preserve the authored roll; Hyper petals remain forge-only.
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
    // Hyper ordinary drops retain Eternal's authored S tail. Extra Eternal
    // payouts are budgeted separately by hyperEternalChances().
    const auto tier=mob==Rarity::Apex?Rarity::Unique:mob;
    return rates.empty()?none:rates[std::min(rates.size()-1,static_cast<std::size_t>(rarityIndex(tier)))];
}
// Negative means a legacy row without an authored tier distribution.
inline double hyperSuperChance(const DropRates& rates) {
    if (rates.empty()) return -1.0;
    double chance=0;
    for(const auto& rate:ratesFor(rates,Rarity::Apex))
        if(rate.rarity==Rarity::Super) chance+=rate.probability;
    return chance;
}
struct HyperEternalDropWeight {
    double superChance = 0.0;
    double eternalOnlyChance = 0.0;
};

inline HyperEternalDropWeight hyperEternalDropWeight(const DropRates& rates, Rarity minimum) {
    HyperEternalDropWeight weight{hyperSuperChance(rates), 0.0};
    if (minimum == Rarity::Unique && weight.superChance == 0.0) {
        for (const auto& rate : ratesFor(rates, Rarity::Apex)) {
            if (rate.rarity == Rarity::Unique) weight.eternalOnlyChance += rate.probability;
        }
    }
    return weight;
}

inline std::vector<double> hyperEternalChances(const std::vector<HyperEternalDropWeight>& weights,
                                               double budget) {
    std::vector<double> result(weights.size(), 0.0);
    if (weights.empty()) return result;

    // Eternal-only petals have no Super tail. Reserve their authored chance
    // inside the same budget, scaling them down together if it is exhausted.
    double eternalOnlyTotal = 0.0;
    double superTotal = 0.0;
    for (const auto& weight : weights) {
        eternalOnlyTotal += weight.eternalOnlyChance;
        superTotal += std::max(0.0, weight.superChance);
    }
    const double reserved = std::min(budget, eternalOnlyTotal);
    const double specialScale = eternalOnlyTotal > 0.0 ? reserved / eternalOnlyTotal : 0.0;
    const double legacyShare = (budget - reserved) / weights.size();
    double remaining = budget - reserved;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i].superChance < 0.0) {
            result[i] = legacyShare;
            remaining -= legacyShare;
        }
    }
    const double superScale = superTotal > 0.0
        ? std::min(0.5, std::max(0.0, remaining) / superTotal) : 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        result[i] += weights[i].eternalOnlyChance * specialScale +
            std::max(0.0, weights[i].superChance) * superScale;
    }
    return result;
}
inline int rollDropRates(const DropRates& rates,Rarity mob,double sample) {
    if(sample<0)return -1;
    double sum=0;
    for(const auto& rate:ratesFor(rates,mob)){sum+=rate.probability;if(sample<sum)return rarityIndex(rate.rarity);}
    return -1;
}
} // namespace flix
