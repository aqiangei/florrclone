#pragma once
#include "server/db.h"
#include "server/timed_hammers.h"
#include "shared/game/hyper_board.h"
#include <algorithm>
#include <map>

namespace flix {
inline std::uint32_t hyperHoldingOwnerRank(const std::vector<HyperHoldingRow>& rows,
                                           const std::string& name) {
    const std::string* previous = nullptr;
    std::uint32_t rank = 0;
    for (const auto& row : rows) {
        if (!previous || *previous != row.name) ++rank;
        if (row.name == name) return rank;
        previous = &row.name;
    }
    return 0;
}

inline std::vector<HyperHoldingRow> hyperHoldingBoard(const Database& db, double now) {
    std::map<std::string, std::map<std::string, std::uint32_t>> claims;
    for (const auto& [key, row] : db.storedTable("hyperForges").members()) {
        if (key.rfind("apex-hammer:", 0) == 0) continue;
        const auto owner = row["owner"].asString();
        if (owner.empty()) continue;
        const auto colon = key.find(':');
        ++claims[owner][colon == std::string::npos ? key : key.substr(colon + 1)];
    }
    std::vector<HyperHoldingRow> rows;
    for (const auto& username : db.usernames()) {
        const auto* account = db.findUser(username);
        if (!account) continue;
        std::map<std::string, std::uint32_t> counts;
        if (const auto* saved = db.findProgress(account->id)) {
            // Inventory is the bag internally; equipped items are separate.
            // Only read the H bag: do not copy every account's entire inventory.
            const auto& record = *saved;
            for (const auto& [key, value] : record.inventory["apex"].members())
                if (key.rfind("petal_", 0) == 0 && value.asInt() > 0)
                    counts[key.substr(6)] = record.itemCount(Rarity::Apex, key);
            for (const auto& slot : record.loadout)
                if (slot && slot->rarity == Rarity::Apex) ++counts[slot->petalType];
            const auto& lease = record.extra["temporaryHyperHammer"];
            if (lease["active"].asBool(false) && lease["expiresAt"].asDouble(0) <= now &&
                counts["mjolnir"] > 0) --counts["mjolnir"];
        }
        // Two titans can hold the same type. Claims represent the same items,
        // not extra copies; released historical rows have no owner above.
        if (const auto held = claims.find(account->id); held != claims.end()) {
            for (const auto& [petal, count] : held->second)
                counts[petal] = std::max(counts[petal], count);
        }
        std::uint64_t total = 0;
        for (const auto& [petal, count] : counts) total += count;
        for (const auto& [petal, count] : counts) if (count)
            rows.push_back({account->username, petal, count,
                static_cast<std::uint32_t>(std::min<std::uint64_t>(total, UINT32_MAX))});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.ownerTotal != b.ownerTotal) return a.ownerTotal > b.ownerTotal;
        if (a.name != b.name) return a.name < b.name;
        return a.petal < b.petal;
    });
    return rows;
}
}
