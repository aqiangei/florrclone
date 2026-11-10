#pragma once
#include "server/db.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace flix {
struct PlaytimeRow { std::string name; double seconds; };
inline std::vector<PlaytimeRow> playtimeBoard(const Database& db, bool includeAdmins) {
    std::vector<PlaytimeRow> rows;
    const auto& activity = db.storedTable("councilActivity");
    for (const auto& name : db.usernames()) {
        const auto* user = db.findUser(name);
        if (!user || user->guest || (user->admin && !includeAdmins)) continue;
        const auto* record = db.findProgress(user->id);
        if (!record) continue;
        const double seconds = std::max(0.0, activity[user->id].asDouble()) +
            std::max(0.0, record->extra["councilActiveSeconds"].asDouble());
        if (std::isfinite(seconds)) rows.push_back({name, seconds});
    }
    const auto shown = std::min<std::size_t>(50, rows.size());
    std::partial_sort(rows.begin(), rows.begin() + shown, rows.end(), [](const auto& a, const auto& b) {
        return a.seconds != b.seconds ? a.seconds > b.seconds : a.name < b.name;
    });
    rows.resize(shown);
    return rows;
}
}
