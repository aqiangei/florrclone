#pragma once

#include <array>

namespace flix {

inline constexpr std::array<int, 5> kDailyStreakRewards{500, 1000, 1500, 2000, 2500};

constexpr int dailyStreakCycleDay(int streak) {
    return streak > 0 ? (streak - 1) % static_cast<int>(kDailyStreakRewards.size()) + 1 : 1;
}

constexpr int dailyStreakReward(int streak) {
    return kDailyStreakRewards[dailyStreakCycleDay(streak) - 1];
}

} // namespace flix
