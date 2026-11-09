#pragma once

#include <array>

namespace flix {

inline constexpr std::array<int, 5> kDailyStreakRewards{10000, 20000, 30000, 40000, 50000};

constexpr int dailyStreakCycleDay(int streak) {
    return streak > 0 ? (streak - 1) % static_cast<int>(kDailyStreakRewards.size()) + 1 : 1;
}

constexpr int dailyStreakReward(int streak) {
    return kDailyStreakRewards[dailyStreakCycleDay(streak) - 1];
}

} // namespace flix
