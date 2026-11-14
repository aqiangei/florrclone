#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "server/db.h"
#include "shared/game/components.h"
#include "shared/game/config.h"
#include "shared/net/bytebuffer.h"

namespace flix {

inline std::array<std::uint32_t, kLoadoutActiveSlots> practiceLoadoutKey(const Loadout& loadout) {
    std::array<std::uint32_t, kLoadoutActiveSlots> key{};
    for (int i = 0; i < kLoadoutActiveSlots; ++i) {
        const auto& slot = loadout.slots[i];
        if (!slot.empty()) key[i] = (static_cast<std::uint32_t>(slot.configIndex) << 8) |
                                   static_cast<std::uint32_t>(rarityIndex(slot.rarity) + 1);
    }
    return key;
}

inline std::string practiceLoadout(const ContentRegistry& content, const Loadout& loadout) {
    Json slots = Json::array();
    for (int i = 0; i < kLoadoutActiveSlots; ++i) {
        const auto& slot = loadout.slots[i];
        if (slot.empty()) continue;
        Json item = Json::object();
        item["slot"] = i + 1;
        item["petal"] = content.petal(slot.configIndex).id;
        item["tier"] = rarityIndex(slot.rarity);
        slots.push(std::move(item));
    }
    return slots.dump();
}

// Keep the best result for each loadout. A public result is immutable until
// the owner deliberately hides or replaces it; later private tests stay private.
inline void savePracticeScore(Database& db, const std::string& userId,
                              const std::string& loadout, double damage,
                              Json breakdown = Json()) {
    if (!std::isfinite(damage) || damage <= 0.0) return;
    auto& record = db.progress(userId);
    auto& history = record.extra["practiceRecords"];
    auto& stored = history["rows"];
    if (!stored.isArray()) stored = Json::array();
    auto& rows = stored.items();
    std::size_t replace = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row["loadout"].asString() != loadout) continue;
        if (row["damage"].asDouble() >= damage) return;
        if (!row["public"].asBool()) replace = i;
    }
    if (replace == rows.size() && rows.size() >= net::kPracticeHistoryLimit) {
        // Never silently revoke a result the player already shared.
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i]["public"].asBool()) continue;
            if (replace == rows.size() || rows[i]["damage"].asDouble() < rows[replace]["damage"].asDouble())
                replace = i;
        }
        if (replace == rows.size() || rows[replace]["damage"].asDouble() >= damage) return;
    }
    Json row = Json::object();
    const auto id = static_cast<std::uint32_t>(history["nextId"].asDouble(1.0));
    history["nextId"] = static_cast<double>(id) + 1.0;
    row["id"] = static_cast<double>(id);
    row["damage"] = damage;
    row["at"] = static_cast<double>(db.nowMillis());
    row["loadout"] = loadout;
    if (breakdown.isArray()) row["breakdown"] = std::move(breakdown);
    if (replace == rows.size()) rows.push_back(std::move(row));
    else rows[replace] = std::move(row);
    db.markDirty();
}

inline void sharePracticeScore(Database& db, const std::string& userId, std::uint32_t id, bool publish) {
    if (id == 0) return;
    auto& rows = db.progress(userId).extra["practiceRecords"]["rows"].items();
    for (auto& row : rows) {
        if (row["id"].asDouble() != id) continue;
        if (row["public"].asBool() != publish) {
            row["public"] = publish;
            db.markDirty();
        }
        return;
    }
}

inline Json practiceBoardView(const Database& db, const std::string& viewer, bool includeAdmins) {
    Json result = Json::object();
    result["own"] = Json::array();
    result["public"] = Json::array();
    double ownBest = 0.0;
    if (const auto* record = db.findProgress(viewer)) {
        result["own"] = record->extra["practiceRecords"]["rows"];
        for (const auto& row : result["own"].items())
            ownBest = std::max(ownBest, row["damage"].asDouble());
    }
    std::vector<std::pair<const Account*, const Json*>> publicRows;
    unsigned ownRank = ownBest > 0 ? 1 : 0;
    for (const auto& userId : db.playerIds()) {
        const auto* user = db.findUserById(userId);
        const auto* record = db.findProgress(userId);
        if (!user || !record || (user->admin && !includeAdmins && userId != viewer)) continue;
        const Json* bestPublic = nullptr;
        double best = 0.0;
        for (const auto& row : record->extra["practiceRecords"]["rows"].items()) {
            const double damage = row["damage"].asDouble();
            best = std::max(best, damage);
            if (damage > 0.0 && row["public"].asBool() &&
                (!bestPublic || damage > (*bestPublic)["damage"].asDouble())) bestPublic = &row;
        }
        if (ownBest > 0.0 && best > ownBest) ++ownRank;
        if (bestPublic && (!user->admin || includeAdmins)) publicRows.emplace_back(user, bestPublic);
    }
    const auto shown = std::min<std::size_t>(publicRows.size(), net::kPracticeBoardRows);
    std::partial_sort(publicRows.begin(), publicRows.begin() + shown, publicRows.end(),
        [](const auto& a, const auto& b) {
            const double first = (*a.second)["damage"].asDouble();
            const double second = (*b.second)["damage"].asDouble();
            return first != second ? first > second : a.first->username < b.first->username;
        });
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& [account, score] = publicRows[i];
        Json row = *score;
        row["name"] = account->username;
        result["public"].push(std::move(row));
    }
    auto& ownRows = result["own"].items();
    std::sort(ownRows.begin(), ownRows.end(),
              [](const auto& a, const auto& b) { return a["damage"].asDouble() > b["damage"].asDouble(); });
    result["rank"] = static_cast<double>(ownRank);
    return result;
}

inline void writePracticeBoard(ByteWriter& out, const Json& board) {
    out.u8(static_cast<std::uint8_t>(net::ServerMessage::PracticeBoard));
    out.u32(static_cast<std::uint32_t>(board["rank"].asDouble()));
    const auto list = [&](const char* key, std::size_t limit) {
        const auto& rows = board[key].items();
        const auto count = std::min(limit, rows.size());
        out.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i) {
            const auto& row = rows[i];
            out.u32(static_cast<std::uint32_t>(row["id"].asDouble()));
            out.str(row["name"].asString());
            out.f64(row["damage"].asDouble());
            out.f64(row["at"].asDouble());
            out.boolean(row["public"].asBool());
            out.str(row["loadout"].asString());
            out.str(row["breakdown"].dump());
        }
    };
    list("own", net::kPracticeHistoryLimit);
    list("public", net::kPracticeBoardRows);
}

} // namespace flix
