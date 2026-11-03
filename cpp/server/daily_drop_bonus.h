#pragma once
#include <chrono>
#include <cstdint>
#include "server/db.h"
namespace flix {
inline std::int64_t dropBonusNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::int64_t dropBonusDay(std::int64_t now) { return (now+8LL*3600000)/86400000; }
inline int dropBonusStreak(const PlayerRecord& r,std::int64_t now) {
    const auto day=static_cast<std::int64_t>(r.extra["dailyDropBonus"]["day"].asDouble(-1));
    return day>=dropBonusDay(now)-1 ? clamp(r.extra["dailyDropBonus"]["streak"].asInt(),0,1000000) : 0;
}
inline bool claimDropBonus(PlayerRecord& r,std::int64_t now) {
    auto& b=r.extra["dailyDropBonus"]; const auto today=dropBonusDay(now);
    const auto last=static_cast<std::int64_t>(b["day"].asDouble(-1));
    if(last>=today)return false;
    const int streak=dropBonusStreak(r,now)+1;
    b["day"]=Json(static_cast<double>(today));b["streak"]=streak;
    b["multiplier"]=streak%4==0?3:2;b["until"]=Json(static_cast<double>(now+3600000));return true;
}
inline int activeDropBonus(const PlayerRecord& r,std::int64_t now) {
    const auto& b=r.extra["dailyDropBonus"];
    return b["until"].asDouble() > now ? clamp(b["multiplier"].asInt(1),1,3) : 1;
}
}