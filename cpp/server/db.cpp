#include "server/db.h"
#include "shared/game/daily_streak.h"
#include "server/hyper_history.h"
#include "server/timed_hammers.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#if defined(__EMSCRIPTEN_PTHREADS__) || (defined(__unix__) && !defined(__EMSCRIPTEN__))
#define FLIX_BACKGROUND_SAVE
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <thread>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "shared/core/civil_date.h"
#include "shared/core/text.h"
#include "shared/game/constants.h"

namespace flix {
namespace {

#ifdef __EMSCRIPTEN__
/// The size of `file` on the HOST's filesystem, asked of Node's own fs: -1
/// when the host has no such file, -2 when there is no Node to ask -- a page,
/// whose only lasting storage is the browser's. Under emscripten the wasm's
/// own stat() answers for its virtual filesystem, where a directory nothing
/// mounted from the host is MEMFS: a file written there opens, fills, flushes
/// and stats exactly like a real one, and is gone when the process is. Only
/// the host can say a file is on disk. The path is the same on both sides
/// because server/main.cpp mounts each host directory at its own absolute
/// path.
double hostFileSize(const std::string& file) {
    return EM_ASM_DOUBLE({
        if (typeof process !== "object" || typeof require !== "function") return -2;
        try {
            const stat = require("fs").statSync(UTF8ToString($0));
            return stat.isFile() ? stat.size : -1;
        } catch (e) {
            return -1;
        }
    }, file.c_str());
}
#endif

/// Days since the epoch for a stored "YYYY-MM-DD", or nullopt when the record
/// holds something else. A record written by hand, or by a version that stored
/// a timestamp, must reset the streak rather than crash the login.
std::optional<std::int64_t> parseUtcDate(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (text[i] < '0' || text[i] > '9') return std::nullopt;
    }
    const int year = std::stoi(text.substr(0, 4));
    const int month = std::stoi(text.substr(5, 2));
    const int day = std::stoi(text.substr(8, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
}

bool fileExists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

/// The database's own whole-file read, deliberately not flix::readFile
/// (shared/core/file.h). That one reads through an iostream, and a filebuf
/// turns a failed read into an early end of file: it hands back what it got
/// and calls it a success. For a content file the parse that follows says the
/// rest. For the database the message a failed load leaves is what an operator
/// acts on -- "read error" says the disk failed and the file may well be fine,
/// where a parse error says the file itself is damaged -- so this reads
/// through stdio and asks ferror. Either way the load fails and writes stay
/// blocked.
bool readWholeFile(const std::string& path, std::string& out, std::string& errorOut) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { errorOut = path + ": cannot open for reading"; return false; }
    out.clear();
    char chunk[64 * 1024];
    while (std::size_t got = std::fread(chunk, 1, sizeof(chunk), f)) out.append(chunk, got);
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) { errorOut = path + ": read error"; return false; }
    return true;
}

bool isOneOf(const std::string& key, std::initializer_list<const char*> known) {
    for (const char* k : known) {
        if (key == k) return true;
    }
    return false;
}

/// Copies every key of `source` that `known` does not claim into `out`. This
/// is what keeps a field the rewrite has never heard of alive across a save.
void collectExtras(const Json& source, std::initializer_list<const char*> known, Json& out) {
    out = Json::object();
    if (!source.isObject()) return;
    for (const std::string& key : source.keys()) {
        if (!isOneOf(key, known)) out[key] = source[key];
    }
}

void appendExtras(const Json& extra, Json& out) {
    if (!extra.isObject()) return;
    for (const std::string& key : extra.keys()) out[key] = extra[key];
}

/// Timestamps go out as doubles rather than through Json's integer
/// constructors: std::int64_t is `long` on Linux and `long long` on macOS, and
/// only one of those has an exact overload. Unix millis are well inside a
/// double's exact integer range for the next 200,000 years.
Json millisJson(std::int64_t millis) { return Json(static_cast<double>(millis)); }

/// The TypeScript build's consumables, as the bare keys it saved them under in
/// an account's rarity buckets, and the petal each one became.
struct LegacyConsumable {
    const char* key;
    const char* petal;
};
constexpr LegacyConsumable kLegacyConsumables[] = {
    {"speed_boost", "petal_powder"},
    {"health_potion", "petal_rose"},
    {"shield", "petal_shell"},
};

/// Folds those consumables into their petals: `{"epic": {"shield": 3}}`
/// becomes `{"epic": {"petal_shell": 3}}`, added to any shells already there.
/// Same tier, same count; the bare key goes. This build has nothing that reads
/// one, so left alone they were dead weight a player could never see or spend.
void convertLegacyConsumables(Json& inventory) {
    if (!inventory.isObject()) return;
    const std::vector<std::string> tiers = inventory.keys();
    for (const std::string& tier : tiers) {
        Json& bucket = inventory[tier];
        if (!bucket.isObject()) continue;
        for (const LegacyConsumable& consumable : kLegacyConsumables) {
            if (!bucket.contains(consumable.key)) continue;
            const int count = PlayerRecord::stackCount(bucket[consumable.key]);
            bucket.erase(consumable.key);
            if (count == 0) continue;
            const std::string petal = consumable.petal;
            const std::int64_t sum =
                static_cast<std::int64_t>(PlayerRecord::stackCount(bucket[petal])) + count;
            bucket[petal] = static_cast<int>(std::min<std::int64_t>(sum, kMaxStackCount));
        }
        if (bucket.size() == 0) inventory.erase(tier);
    }
}

/// Mob ids that were folded into another, and the mob each one became. The
/// sponge was two mobs, one per colour, until it became one mob that picks its
/// colour per entity (2026-10-26).
struct LegacyMob {
    const char* id;
    const char* mob;
};
constexpr LegacyMob kLegacyMobs[] = {
    {"sponge_1", "sponge"},
    {"sponge_2", "sponge"},
};

/// Folds a retired mob's kills into the mob it became, tier by tier, so the
/// gallery keeps the count it showed: `{"sponge_1": {"epic": 2}, "sponge_2":
/// {"epic": 3}}` becomes `{"sponge": {"epic": 5}}`. The old key goes -- this
/// build has no mob by that id, so it would only ever sit in the record unseen.
void convertLegacyMobKills(Json& mobKills) {
    if (!mobKills.isObject()) return;
    for (const LegacyMob& legacy : kLegacyMobs) {
        if (!mobKills.contains(legacy.id)) continue;
        const Json old = mobKills[legacy.id];
        mobKills.erase(legacy.id);
        if (!old.isObject()) continue;
        Json& into = mobKills[legacy.mob];
        for (const std::string& tier : old.keys()) {
            const std::int64_t sum = std::max<std::int64_t>(0, into[tier].asInt64(0)) +
                                     std::max<std::int64_t>(0, old[tier].asInt64(0));
            into[tier] = static_cast<int>(std::min<std::int64_t>(sum, kMaxStackCount));
        }
    }
}

// -- record <-> JSON ---------------------------------------------------------

PlayerRecord playerFromJson(const Json& value) {
    PlayerRecord record;
    if (!value.isObject()) return record;

    record.totalXp = value["totalXP"].asDouble(0);
    record.talentPointBonus = std::max(0, value["talentPointBonus"].asInt(0));
    record.stars = value["stars"].asDouble(0);
    record.dailyStreak = value["dailyStreak"].asInt(0);
    record.lastStreakDate = value["lastStreakDate"].asString();
    record.renderFlags = static_cast<std::uint32_t>(value["renderFlags"].asDouble(0));
    record.equippedSkinId = value["equippedSkinId"].asString();

    if (value["inventory"].isObject()) record.inventory = value["inventory"];
    convertLegacyConsumables(record.inventory);
    if (value["mobKills"].isObject()) record.mobKills = value["mobKills"];
    convertLegacyMobKills(record.mobKills);
    // Branches this build does not know are dropped rather than kept as raw
    // JSON: a tier only means something to the code that applies it, and
    // round-tripping one would let it silently reappear as a bonus.
    const auto readSkills = [](const Json& skills, SkillSet& out, Json* unknown) {
        if (!skills.isObject()) return;
        for (const std::string& key : skills.keys()) {
            const SkillId id = skillFromKey(key);
            if (id == SkillId::Count) {
                if (unknown != nullptr) (*unknown)[key] = skills[key];
                continue;
            }
            // florrclone stores 1-based integer levels (0 = none). Older
            // flowrix files stored a rarity name for the highest tier.
            int tier = -1;
            const Json& cell = skills[key];
            if (cell.isNumber()) {
                const int florr = static_cast<int>(cell.asDouble(0));
                tier = florr > 0 ? florr - 1 : -1;
            } else if (cell.isString()) {
                tier = rarityIndex(parseRarity(cell.asString()));
            }
            if (tier < 0) continue;
            out.set(id, std::min(tier, skillTierCount(id) - 1));
        }
        // Removed invalid child nodes refund themselves because TP is derived
        // from spent(); preserve every valid branch and migration bonus.
        for(const auto& gate:kSkillGates)
            if(out.level(gate.child)>=0 && !out.prerequisiteMet(gate.child))out.set(gate.child,-1);

    };
    readSkills(value["skills"], record.skills, nullptr);
    record.mazeTotalXp = value["mazeTotalXP"].asDouble(0);
    readSkills(value["mazeSkills"], record.mazeSkills, &record.mazeSkillsExtra);
    const auto readSlots = [](const Json& slots) {
        std::vector<std::optional<StoredItem>> out;
        for (const Json& slot : slots.items()) {
            if (slot.isObject()) out.push_back(StoredItem::fromJson(slot));
            else out.push_back(std::nullopt);
        }
        return out;
    };
    if (value["loadout"].isArray()) record.loadout = readSlots(value["loadout"]);
    // A record marked `loadoutInInventory` stores worn petals in the bag too
    // (see playerToJson), so each slot claims its petal back out of it here.
    // A slot the bag cannot cover is dropped, never conjured: whatever went
    // wrong with the loadout, the inventory is what the account owns. An
    // unmarked record is the old shape -- worn petals already out of the bag.
    if (value["loadoutInInventory"].asBool(false)) {
        for (std::optional<StoredItem>& slot : record.loadout) {
            if (!slot) continue;
            const std::string key = "petal_" + slot->petalType;
            if (slot->petalType.empty() || record.itemCount(slot->rarity, key) < 1) {
                slot.reset();
                continue;
            }
            record.addItem(slot->rarity, key, -1);
        }
    }
    if (value["loadoutPresets"].isObject()) {
        const Json& presets = value["loadoutPresets"];
        for (const std::string& name : presets.keys()) {
            if (presets[name].isArray()) record.loadoutPresets[name] = readSlots(presets[name]);
        }
    }

    collectExtras(value, {"totalXP", "stars", "dailyStreak", "lastStreakDate", "renderFlags",
                          "equippedSkinId", "inventory", "mobKills", "loadout",
                          "loadoutInInventory", "loadoutPresets", "skills", "tp", "talentPointBonus", "mazeTotalXP", "mazeSkills", "mazeTp"},
                  record.extra);
    return record;
}

Json playerToJson(const PlayerRecord& record) {
    Json out = Json::object();
    if (record.talentPointBonus > 0) out["talentPointBonus"] = record.talentPointBonus;
    out["totalXP"] = record.totalXp;
    // Live, an equipped petal is out of the bag. Stored, it is still in it,
    // and the loadout only names which ones are worn: a loadout that is lost
    // or garbled in the file then costs a re-equip, not the petals.
    PlayerRecord stored;
    stored.inventory = record.inventory;
    for (const std::optional<StoredItem>& slot : record.loadout) {
        if (slot && !slot->petalType.empty()) stored.addItem(slot->rarity, "petal_" + slot->petalType, 1);
    }
    out["inventory"] = std::move(stored.inventory);
    out["loadoutInInventory"] = true;
    const auto writeSlots = [](const std::vector<std::optional<StoredItem>>& slots) {
        Json out = Json::array();
        for (const std::optional<StoredItem>& slot : slots) {
            out.push(slot ? slot->toJson() : Json());
        }
        return out;
    };
    if (!record.loadout.empty()) out["loadout"] = writeSlots(record.loadout);
    if (!record.loadoutPresets.empty()) {
        Json presets = Json::object();
        for (const auto& [name, slots] : record.loadoutPresets) presets[name] = writeSlots(slots);
        out["loadoutPresets"] = presets;
    }
    if (record.mobKills.isObject() && record.mobKills.size() > 0) out["mobKills"] = record.mobKills;
    const auto writeSkills = [](const SkillSet& set, Json skills) {
        for (int i = 0; i < kSkillCount; ++i) {
            const int florr = set.florrLevel(static_cast<SkillId>(i));
            if (florr <= 0) continue;
            // Match florrclone `save.talents`: integer levels, 0 omitted.
            skills[kSkillKeys[static_cast<std::size_t>(i)]] = florr;
        }
        return skills;
    };
    const Json skills = writeSkills(record.skills, Json::object());
    if (skills.size() > 0) {
        out["skills"] = skills;
        // `tp` is derived here and never read back. It is still written
        // because files the TypeScript build wrote carry it beside `skills`,
        // so a save diffs cleanly against an old backup instead of dropping it.
        out["tp"] = record.talentPoints();
    }
    // The maze track, on the same terms and only when it carries anything.
    if (record.mazeTotalXp != 0) out["mazeTotalXP"] = record.mazeTotalXp;
    const Json mazeSkills = writeSkills(record.mazeSkills, record.mazeSkillsExtra);
    if (mazeSkills.size() > 0) {
        out["mazeSkills"] = mazeSkills;
        out["mazeTp"] = record.mazeTalentPoints();
    }
    // Optional fields are written only when they carry information. A missing
    // key and an explicit 0/false read identically to every consumer, and this
    // file has one record per account -- the noise is not free.
    if (record.stars != 0) out["stars"] = record.stars;
    if (record.renderFlags != 0) out["renderFlags"] = static_cast<double>(record.renderFlags);
    if (!record.equippedSkinId.empty()) out["equippedSkinId"] = record.equippedSkinId;
    if (record.dailyStreak != 0) out["dailyStreak"] = record.dailyStreak;
    if (!record.lastStreakDate.empty()) out["lastStreakDate"] = record.lastStreakDate;
    appendExtras(record.extra, out);
    return out;
}

Account accountFromJson(const Json& value, const std::string& storedKey) {
    Account account;
    if (!value.isObject()) { account.username = storedKey; return account; }

    account.id = value["id"].asString();
    account.username = value["username"].asString(storedKey);
    // cpp inventory uses `password`; florrclone migrate may also write
    // `passwordHash` (same hex string) beside a `salt`.
    account.passwordHash = value["password"].asString();
    if (account.passwordHash.empty()) account.passwordHash = value["passwordHash"].asString();
    account.passwordSalt = value["salt"].asString();
    account.isPlainText = value["isPlainText"].asBool(false);
    account.admin = value["admin"].asBool(false);
    if (value["owner"].asBool(false)) {
        // The server owner, by its own field and only a JSON `true`: never a
        // number in adminTier, where a mistyped value must read low, not high.
        // It outranks any adminTier beside it.
        account.adminTier = kOwnerAdminTier;
    } else if (value.contains("adminTier")) {
        // Hand-written on the server's machine, so read strictly: a value that
        // is not a whole number from 1 to 4 is the bottom tier, never the top.
        const Json& tier = value["adminTier"];
        const double number = tier.isNumber() ? tier.asDouble(0) : 0;
        const bool whole = number >= kTopAdminTier && number <= kBottomAdminTier &&
                           number == std::floor(number);
        account.adminTier = whole ? static_cast<int>(number) : kBottomAdminTier;
    }
    account.adminBanned = value["adminBanned"].asBool(false);
    account.adminBannedAtMillis = static_cast<std::int64_t>(value["adminBannedAt"].asDouble(0));
    account.adminBannedBy = value["adminBannedBy"].asString();
    account.lastActiveAtMillis = static_cast<std::int64_t>(value["lastActiveAt"].asDouble(0));
    account.muted = value["muted"].asBool(false);
    account.mutedAtMillis = static_cast<std::int64_t>(value["mutedAt"].asDouble(0));
    account.mutedBy = value["mutedBy"].asString();
    account.createdAtMillis = static_cast<std::int64_t>(value["createdAt"].asDouble(0));
    account.createdFromHash = value["createdFromHash"].asString();
    account.guest = value["guest"].asBool(false);
    account.passwordChosen = value["passwordChosen"].asBool(true);

    collectExtras(value, {"id", "username", "password", "passwordHash", "salt", "isPlainText",
                          "admin", "owner", "adminTier", "adminBanned", "adminBannedAt",
                          "adminBannedBy", "lastActiveAt", "muted", "mutedAt", "mutedBy",
                          "createdAt", "createdFromHash", "guest", "passwordChosen"},
                  account.extra);
    return account;
}

Json accountToJson(const Account& account) {
    Json out = Json::object();
    out["id"] = account.id;
    out["username"] = account.username;
    out["password"] = account.passwordHash;
    if (!account.passwordSalt.empty()) out["salt"] = account.passwordSalt;
    if (account.isPlainText) out["isPlainText"] = true;
    if (account.admin) out["admin"] = true;
    // Kept whether or not `admin` is set, so taking the flag away for a while
    // and giving it back does not quietly promote the account to tier 1.
    if (account.adminTier == kOwnerAdminTier) {
        out["owner"] = true;
    } else if (account.adminTier != kTopAdminTier) {
        out["adminTier"] = account.adminTier;
    }
    if (account.adminBanned) {
        out["adminBanned"] = true;
        if (account.adminBannedAtMillis != 0) {
            out["adminBannedAt"] = millisJson(account.adminBannedAtMillis);
        }
        if (!account.adminBannedBy.empty()) out["adminBannedBy"] = account.adminBannedBy;
    }
    if (account.lastActiveAtMillis != 0) out["lastActiveAt"] = millisJson(account.lastActiveAtMillis);
    if (account.muted) {
        out["muted"] = true;
        if (account.mutedAtMillis != 0) out["mutedAt"] = millisJson(account.mutedAtMillis);
        if (!account.mutedBy.empty()) out["mutedBy"] = account.mutedBy;
    }
    // Both omitted when unset, so accounts written before the registration
    // limits existed round-trip through this build unchanged.
    if (account.createdAtMillis != 0) out["createdAt"] = millisJson(account.createdAtMillis);
    if (!account.createdFromHash.empty()) out["createdFromHash"] = account.createdFromHash;
    // Written only while they say something, for the same reason: every
    // account that predates them is a registered one with a typed password.
    if (account.guest) out["guest"] = true;
    if (!account.passwordChosen) out["passwordChosen"] = false;
    appendExtras(account.extra, out);
    return out;
}

Database::Session sessionFromJson(const Json& value, const std::string& storedKey) {
    Database::Session session;
    session.tokenHash = value["tokenHash"].asString(storedKey);
    session.userId = value["userId"].asString();
    session.username = value["username"].asString();
    session.createdAtMillis = static_cast<std::int64_t>(value["createdAt"].asDouble(0));
    session.expiresAtMillis = static_cast<std::int64_t>(value["expiresAt"].asDouble(0));
    return session;
}

Json sessionToJson(const Database::Session& session) {
    Json out = Json::object();
    out["tokenHash"] = session.tokenHash;
    out["userId"] = session.userId;
    out["username"] = session.username;
    out["createdAt"] = millisJson(session.createdAtMillis);
    out["expiresAt"] = millisJson(session.expiresAtMillis);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// StoredItem
// ---------------------------------------------------------------------------

StoredItem StoredItem::fromJson(const Json& value) {
    StoredItem item;
    if (!value.isObject()) return item;

    item.type = value["type"].asString("petal");
    item.rarity = parseRarity(value["rarity"].asString("common"));
    item.petalType = value["petalType"].asString();
    if (value["health"].isNumber()) {
        item.health = value["health"].asDouble();
        item.hasHealth = true;
    }
    if (value["maxHealth"].isNumber()) {
        item.maxHealth = value["maxHealth"].asDouble();
        item.hasMaxHealth = true;
    }
    collectExtras(value, {"type", "rarity", "petalType", "health", "maxHealth"}, item.extra);
    return item;
}

Json StoredItem::toJson() const {
    Json out = Json::object();
    out["type"] = type;
    out["rarity"] = std::string(rarityName(rarity));
    if (!petalType.empty()) out["petalType"] = petalType;
    if (hasHealth) out["health"] = health;
    if (hasMaxHealth) out["maxHealth"] = maxHealth;
    appendExtras(extra, out);
    return out;
}

// ---------------------------------------------------------------------------
// PlayerRecord
// ---------------------------------------------------------------------------

int PlayerRecord::stackCount(const Json& stored) {
    const double count = stored.asDouble(0);
    // Written so a NaN fails both tests and reads as empty.
    if (!(count >= 1.0)) return 0;
    if (!(count < static_cast<double>(kMaxStackCount))) return kMaxStackCount;
    return static_cast<int>(count);
}

int PlayerRecord::itemCount(Rarity rarity, const std::string& itemType) const {
    const Json& bucket = inventory[std::string(rarityName(rarity))];
    if (!bucket.isObject()) return 0;
    return stackCount(bucket[itemType]);
}

void PlayerRecord::setItemCount(Rarity rarity, const std::string& itemType, int count) {
    const std::string tier = rarityName(rarity);
    if (count <= 0) {
        if (!inventory.contains(tier)) return;
        Json& bucket = inventory[tier];
        if (!bucket.isObject()) return;
        bucket.erase(itemType);
        // An empty tier is dropped so the file does not accumulate rubble from
        // every petal a player has ever crafted away.
        if (bucket.size() == 0) inventory.erase(tier);
        return;
    }
    // operator[] promotes a missing or non-object tier into an object.
    inventory[tier][itemType] = count;
}

void PlayerRecord::addItem(Rarity rarity, const std::string& itemType, int delta) {
    // Summed wide: a full stack plus one is past int, and the wrapped result
    // is negative -- which setItemCount reads as "erase the stack".
    const std::int64_t sum = static_cast<std::int64_t>(itemCount(rarity, itemType)) + delta;
    setItemCount(rarity, itemType,
                 static_cast<int>(std::min<std::int64_t>(sum, kMaxStackCount)));
}

int PlayerRecord::killCount(const std::string& mobType, Rarity rarity) const {
    const Json& bucket = mobKills[mobType];
    if (!bucket.isObject()) return 0;
    return bucket[std::string(rarityName(rarity))].asInt(0);
}

void PlayerRecord::recordKill(const std::string& mobType, Rarity rarity) {
    const std::string tier = rarityName(rarity);
    mobKills[mobType][tier] = killCount(mobType, rarity) + 1;
}

int PlayerRecord::talentPoints() const {
    return availableTalentPoints(levelFromTotalXp(totalXp).level, skills, talentPointBonus);
}

int PlayerRecord::mazeTalentPoints() const {
    return availableTalentPoints(levelFromTotalXp(mazeTotalXp).level, mazeSkills);
}

// ---------------------------------------------------------------------------
// Database: lifecycle and persistence
// ---------------------------------------------------------------------------

#ifdef FLIX_BACKGROUND_SAVE
namespace {
enum class SnapshotOutcome { Failed, Saved, Uncertain };

#ifdef __EMSCRIPTEN__
// Runs on the persistence pthread. NODEFS and libc output can proxy to the
// main thread, which may be waiting for this write during a graceful stop.
EM_JS(int, flix_write_snapshot, (const char* file, const char* text, std::size_t size), {
    const fs = require("node:fs");
    const path = UTF8ToString(file);
    let temp, fd, dir, renamed = false;
    try {
        dir = fs.openSync(require("node:path").dirname(path), "r");
        temp = path + ".save-" + require("node:crypto").randomUUID();
        fd = fs.openSync(temp, "wx", 0o600);
        const bytes = Buffer.from(HEAPU8.subarray(text, text + size));
        fs.writeFileSync(fd, bytes);
        if (fs.fstatSync(fd).size !== size) throw new Error("short write");
        fs.fsyncSync(fd);
        fs.closeSync(fd);
        fd = undefined;
        fs.renameSync(temp, path);
        renamed = true;
        fs.fsyncSync(dir);
        return 1;
    } catch (error) {
        try { fs.writeSync(2, "[persist-worker] " + error.message + "\n"); } catch (_) {}
        return renamed ? 2 : 0;
    } finally {
        if (fd !== undefined) try { fs.closeSync(fd); } catch (_) {}
        if (dir !== undefined) try { fs.closeSync(dir); } catch (_) {}
        if (temp && !renamed) try { fs.unlinkSync(temp); } catch (_) {}
    }
});
#endif

SnapshotOutcome writeSnapshot(const std::string& path, const std::string& text) {
#ifdef __EMSCRIPTEN__
    return static_cast<SnapshotOutcome>(flix_write_snapshot(path.c_str(), text.data(), text.size()));
#else
    const auto slash = path.find_last_of('/');
    const auto directory = slash == std::string::npos ? "." : path.substr(0, slash + 1);
    const int dir = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir < 0) return SnapshotOutcome::Failed;
    std::string temp = path + ".save-XXXXXX";
    const int fd = ::mkstemp(temp.data());
    if (fd < 0) {
        ::close(dir);
        return SnapshotOutcome::Failed;
    }
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto written = ::write(fd, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        offset += static_cast<std::size_t>(written);
    }
    const bool flushed = offset == text.size() && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    SnapshotOutcome outcome = SnapshotOutcome::Failed;
    if (flushed && closed && ::rename(temp.c_str(), path.c_str()) == 0)
        outcome = ::fsync(dir) == 0 ? SnapshotOutcome::Saved : SnapshotOutcome::Uncertain;
    else
        ::unlink(temp.c_str());
    ::close(dir);
    return outcome;
#endif
}
} // namespace

struct Database::SnapshotWriter {
    template<class Row>
    struct Changes {
        typename Table<Row>::RowChanges rows;
        std::optional<std::vector<std::string>> order;
    };
    struct Job {
        Changes<Account> users;
        Changes<PlayerRecord> players;
        Changes<Session> sessions;
        Changes<Json> globals;
        std::vector<std::string> topOrder;
        std::uint64_t generation = 0;
        bool finalSave = false;
    };
    struct Result {
        SnapshotOutcome outcome;
        std::uint64_t generation;
        std::size_t bytes, accounts, players;
        double millis;
        std::string sha;
    };
    struct Rows {
        std::unordered_map<std::string, std::string> text;
        std::vector<std::string> order;

        template<class Row, class Encode>
        void apply(Changes<Row>& changes, const Encode& encode) {
            for (auto& row : changes.rows) {
                if (row.cachedText)
                    text.insert_or_assign(std::move(row.key), std::move(*row.cachedText));
                else if (row.value)
                    text.insert_or_assign(std::move(row.key), encode(row.value->take()));
                else
                    text.erase(row.key);
            }
            // Return row ownership before the file is assembled or written.
            changes.rows.clear();
            if (changes.order) order = std::move(*changes.order);
        }
        void appendTo(std::string& output) const {
            char separator = '{';
            for (const auto& key : order) {
                output += separator;
                separator = ',';
                output += jsonEscape(key);
                output += ':';
                output += text.at(key);
            }
            if (order.empty()) output += '{';
            output += '}';
        }
    };

    explicit SnapshotWriter(const std::string& path)
        : path(path), thread([this] { run(); }) {}
    ~SnapshotWriter() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        ready.notify_one();
        thread.join();
    }

    void submit(Job job) {
        {
            std::lock_guard lock(mutex);
            busy = true;
            pending = std::move(job);
        }
        ready.notify_one();
    }
    bool writing() {
        std::lock_guard lock(mutex);
        return busy;
    }
    std::optional<Result> takeResult(bool wait) {
        std::unique_lock lock(mutex);
        if (wait) finished.wait(lock, [this] { return !busy; });
        return std::exchange(result, std::nullopt);
    }

    bool requested = false;
    std::chrono::steady_clock::time_point retryAt{};
    std::optional<Result> latest;

private:
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] { return stopping || pending.has_value(); });
                if (!pending) return;
                job = std::move(*pending);
                pending.reset();
            }
            auto done = write(job);
            {
                std::lock_guard lock(mutex);
                result = std::move(done);
                busy = false;
            }
            finished.notify_one();
        }
    }
    Result write(Job& job) {
        const auto started = std::chrono::steady_clock::now();
        users.apply(job.users, [](const Account& row) { return accountToJson(row).dump(0); });
        players.apply(job.players, [](const PlayerRecord& row) { return playerToJson(row).dump(0); });
        sessions.apply(job.sessions, [](const Session& row) { return sessionToJson(row).dump(0); });
        globals.apply(job.globals, [](const Json& row) { return row.dump(0); });
        std::string output;
        output.reserve(lastBytes);
        const auto add = [&](const std::string& key, const auto& append) {
            output += output.empty() ? '{' : ',';
            output += jsonEscape(key);
            output += ':';
            append();
        };
        const auto table = [&](const std::string& key, const Rows& rows) {
            add(key, [&] { rows.appendTo(output); });
        };
        bool wroteUsers = false, wrotePlayers = false, wroteSessions = false;
        for (const auto& key : job.topOrder) {
            if (key == "players") { table(key, players); wrotePlayers = true; }
            else if (key == "users") { table(key, users); wroteUsers = true; }
            else if (key == "sessions") { table(key, sessions); wroteSessions = true; }
            else add(key, [&] { output += globals.text.at(key); });
        }
        if (!wrotePlayers) table("players", players);
        if (!wroteUsers) table("users", users);
        if (!wroteSessions && !sessions.order.empty()) table("sessions", sessions);
        output += '}';
        lastBytes = output.size();
        const auto outcome = writeSnapshot(path, output);
        const std::string sha = outcome == SnapshotOutcome::Saved && job.finalSave
            ? crypto::sha256Hex(output) : "";
        const double millis = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return {outcome, job.generation, lastBytes, users.order.size(), players.order.size(), millis, sha};
    }

    std::string path;
    std::size_t lastBytes = 0;
    Rows users, players, sessions, globals;
    std::mutex mutex;
    std::condition_variable ready, finished;
    std::optional<Job> pending;
    std::optional<Result> result;
    bool busy = false, stopping = false;
    std::thread thread;
};
#else
struct Database::SnapshotWriter {};
#endif

Database::Database() = default;

Database::~Database() {
    // The rate limit means the last change of a session is usually still in
    // memory when the process is asked to stop. Flushing here is what turns
    // an orderly shutdown into a lossless one.
    // The owner must explicitly flush authoritative state while its transport
    // and ownership token still exist; never perform RPC from destruction.
    if (!authoritative() && dirty_ && !loadFailed_ && !path_.empty()) save();
}

void Database::reset() {
    snapshotWriter_.reset();
    snapshotHyperHolders_.clear();
    users_.clear();
    players_.clear();
    sessions_.clear();
    usersByLowerName_.clear();
    usersById_.clear();
    usersByCreator_.clear();
    otherTop_.clear();
    otherTopText_.clear();
    topKeyOrder_.clear();
    lastSaveBytes_ = 0;
    path_.clear();
    loadFailed_ = false;
    dirty_ = false;
    everWrote_ = false;
    lastWriteMillis_ = 0;
}

bool Database::load(const std::string& path, std::string& errorOut) {
    if (authoritative()) {
        errorOut = "authoritative database cannot fall back to a local file";
        return false;
    }
    reset();
    path_ = path;

    if (!fileExists(path)) {
        // A first run, not a failure. Dirty so the file appears on the first
        // save rather than only once a player happens to earn something.
        dirty_ = true;
        return true;
    }

    // Each stage lets go of what the next one no longer needs. A wasm heap
    // only grows, so whatever the load holds at its peak stays resident for as
    // long as the server runs: this is the server's floor, not a passing
    // spike. Holding the text, the whole tree and every record at once, then
    // warming the save cache on top, took the prod database (15 MB, ~950k
    // values) to a 276 MB peak for 133 MB of records -- and the heap to 307 MB,
    // on a box with 909 MB and nothing else to give.
    {
        Json root;
        {
            std::string text;
            if (!readWholeFile(path, text, errorOut)) {
                loadFailed_ = true;
                return false;
            }
            std::string parseError;
            if (!Json::parse(text, root, parseError)) {
                loadFailed_ = true;
                errorOut = path + ": " + parseError;
                return false;
            }
        }
        if (!parseRoot(root, errorOut)) {
            loadFailed_ = true;
            return false;
        }
    }
    // File order is not creation order, and creatorStanding walks newest first
    // and stops at its window.
    for (auto& entry : usersByCreator_) {
        std::stable_sort(entry.second.begin(), entry.second.end(),
                         [&](const std::string& a, const std::string& b) {
                             const Account* left = std::as_const(*this).findUserById(a);
                             const Account* right = std::as_const(*this).findUserById(b);
                             return (left ? left->createdAtMillis : 0) <
                                    (right ? right->createdAtMillis : 0);
                         });
    }
    pruneExpiredSessions();
    // Every row is uncached straight off the parse. Paying for that here, at
    // boot, is what keeps the FIRST save -- thirty seconds in, with the
    // players who reconnected after a restart already on -- off the tick.
    lastSaveBytes_ = serialise().size();
    return true;
}

bool Database::loadAuthoritative(const std::string& snapshot, CommitSink sink,
                                 std::string& errorOut) {
    // Only initialization is supported. Replacing a running database would
    // invalidate handler references and could discard dirty live progress.
    if (!path_.empty() || authoritative() || !sink) {
        errorOut = "authoritative database requires an unopened database and a commit sink";
        return false;
    }
    commitSink_ = std::move(sink);
    Json root;
    if (!Json::parse(snapshot, root, errorOut) || !parseRoot(root, errorOut)) {
        loadFailed_ = true;
        return false;
    }
    for (auto& entry : usersByCreator_) {
        std::stable_sort(entry.second.begin(), entry.second.end(),
                        [&](const std::string& a, const std::string& b) {
            const auto* left = std::as_const(*this).findUserById(a);
            const auto* right = std::as_const(*this).findUserById(b);
            return (left ? left->createdAtMillis : 0) <
                   (right ? right->createdAtMillis : 0);
        });
    }
    lastSaveBytes_ = serialise().size();
    players_.acceptLoadedState();users_.acceptLoadedState();sessions_.acceptLoadedState();
    committedTop_.clear();for(const auto& entry:otherTopText_)committedTop_[entry.first]=entry.second;
    touchedTop_.clear();
    return true;
}

bool Database::refreshAuthoritative(const std::string& snapshot,std::string& errorOut) {
    if(!authoritative()||dirty_||commitPending()||commitUncertain_||loadFailed_){errorOut="database is not at a committed boundary";return false;}
    Database staged;
    if(!staged.loadAuthoritative(snapshot,[](const std::string&){return CommitOutcome::Rejected;},errorOut))return false;
    using std::swap;
    swap(users_,staged.users_);swap(players_,staged.players_);swap(sessions_,staged.sessions_);
    swap(usersByLowerName_,staged.usersByLowerName_);swap(usersById_,staged.usersById_);swap(usersByCreator_,staged.usersByCreator_);
    swap(otherTop_,staged.otherTop_);swap(otherTopText_,staged.otherTopText_);swap(topKeyOrder_,staged.topKeyOrder_);
    swap(lastSaveBytes_,staged.lastSaveBytes_);
    swap(committedTop_,staged.committedTop_);swap(touchedTop_,staged.touchedTop_);
    return true;
}

bool Database::replaceAuthoritativeProgress(const std::string& id,const Json& record) {
    if(!authoritative()||commitUncertain_||loadFailed_||!record.isObject())return false;
    players_.insert(id)=playerFromJson(record);
    Json accepted=Json::object();accepted[id]=playerToJson(*std::as_const(players_).find(id));
    players_.acknowledge(accepted,playerToJson);
    return true;
}

bool Database::applyAuthoritativeDelta(const Json& delta,bool allowUnrelatedChanges) {
    if(!authoritative()||commitUncertain_||loadFailed_||!delta.isObject())return false;
    if(!allowUnrelatedChanges&&(dirty_||commitPending()))return false;
    for(const auto* table:{"users","players","sessions","globals"})
        if(!delta[table].isNull()&&!delta[table].isObject())return false;
    for(const auto* table:{"users","players","sessions"})
        for(const auto& row:delta[table].members())if(!row.second.isNull()&&!row.second.isObject())return false;
    if(allowUnrelatedChanges) {
        const auto local=collectDelta();
        for(const auto* table:{"users","players","sessions","globals"})for(const auto& row:delta[table].members())
            if(local[table].contains(row.first)||pendingDelta_[table].contains(row.first))return false;
    }
    Json normalized=delta;
    for(const auto& row:delta["users"].members()) {
        if(row.second.isNull())users_.erase(row.first);
        else {auto& value=users_.insert(row.first);value=accountFromJson(row.second,row.first);normalized["users"][row.first]=accountToJson(value);}
    }
    if(delta["users"].size()) {
        usersByLowerName_.clear();usersById_.clear();usersByCreator_.clear();
        for(const auto& name:users_.keys())indexUser(name,*std::as_const(users_).find(name));
        for(auto& entry:usersByCreator_)std::stable_sort(entry.second.begin(),entry.second.end(),[&](const auto& a,const auto& b){
            return std::as_const(*this).findUserById(a)->createdAtMillis<std::as_const(*this).findUserById(b)->createdAtMillis;
        });
    }
    for(const auto& row:delta["players"].members()) {
        if(row.second.isNull())players_.erase(row.first);
        else {auto& value=players_.insert(row.first);value=playerFromJson(row.second);normalized["players"][row.first]=playerToJson(value);}
    }
    for(const auto& row:delta["sessions"].members()) {
        if(row.second.isNull())sessions_.erase(row.first);
        else {auto& value=sessions_.insert(row.first);value=sessionFromJson(row.second,row.first);normalized["sessions"][row.first]=sessionToJson(value);}
    }
    for(const auto& row:delta["globals"].members()) {
        if(std::find(topKeyOrder_.begin(),topKeyOrder_.end(),row.first)==topKeyOrder_.end())topKeyOrder_.push_back(row.first);
        otherTop_.insert(row.first)=row.second;otherTopText_.erase(row.first);
    }
    acknowledgeDelta(normalized);return true;
}

bool Database::parseRoot(Json& root, std::string& errorOut) {
    if (!root.isObject()) {
        errorOut = path_ + ": top level is not a JSON object";
        return false;
    }
    topKeyOrder_ = root.keys();

    // Consumes `root`: a row's subtree is released the moment it is a record,
    // and the tables kept as JSON are moved rather than copied, so the tree
    // and what it becomes never both exist whole.
    for (const std::string& key : topKeyOrder_) {
        Json& value = root[key];
        // A collection that is present but is not an object is corruption, not
        // an old schema. Treating it as "no accounts" and carrying on is how a
        // damaged file becomes an empty one.
        if (key == "players" || key == "users" || key == "sessions") {
            if (!value.isObject()) {
                errorOut = path_ + ": \"" + key + "\" is not a JSON object";
                return false;
            }
        }

        if (key == "players") {
            for (const std::string& userId : value.keys()) {
                Json& row = value[userId];
                players_.insert(userId) = playerFromJson(row);
                row = Json();
            }
        } else if (key == "users") {
            for (const std::string& name : value.keys()) {
                Json& row = value[name];
                Account& account = users_.insert(name);
                account = accountFromJson(row, name);
                indexUser(name, account);
                row = Json();
            }
        } else if (key == "sessions") {
            for (const std::string& tokenHash : value.keys()) {
                Json& row = value[tokenHash];
                sessions_.insert(tokenHash) = sessionFromJson(row, tokenHash);
                row = Json();
            }
        } else {
            otherTop_.insert(key) = std::move(value);
        }
        value = Json();
    }
    return true;
}

Json& Database::rawArrayTable(const std::string& key) {
    if (authoritative()) touchedTop_.insert(key);
    otherTopText_.erase(key);
    Json& table = otherTop_.insert(key);
    if (!table.isArray()) table = Json::array();
    if (std::find(topKeyOrder_.begin(), topKeyOrder_.end(), key) == topKeyOrder_.end()) {
        topKeyOrder_.push_back(key);
    }
    return table;
}

Json& Database::rawTable(const std::string& key) {
    if (authoritative()) touchedTop_.insert(key);
    otherTopText_.erase(key);
    Json& table = otherTop_.insert(key);
    if (!table.isObject()) table = Json::object();
    if (std::find(topKeyOrder_.begin(), topKeyOrder_.end(), key) == topKeyOrder_.end()) {
        topKeyOrder_.push_back(key);
    }
    return table;
}

std::vector<std::string> Database::storedTableNames() const {
    std::vector<std::string> names;
    for (const std::string& key : otherTop_.keys()) {
        if (key == "ipSalt") continue;
        const Json& table = storedTable(key);
        if (table.isObject() || table.isArray()) names.push_back(key);
    }
    return names;
}

Json playerRecordJson(const PlayerRecord& record) { return playerToJson(record); }
PlayerRecord parsePlayerRecord(const Json& value) { return playerFromJson(value); }
Json accountRecordJson(const Account& account) { return accountToJson(account); }
Account parseAccountRecord(const Json& value, const std::string& storedKey) {
    return accountFromJson(value, storedKey);
}

Json Database::toJson() const {
    Json players = Json::object();
    for (const std::string& userId : players_.keys()) {
        players[userId] = playerToJson(*players_.find(userId));
    }
    Json users = Json::object();
    for (const std::string& name : users_.keys()) {
        users[name] = accountToJson(*users_.find(name));
    }
    Json sessions = Json::object();
    for (const std::string& tokenHash : sessions_.keys()) {
        sessions[tokenHash] = sessionToJson(*sessions_.find(tokenHash));
    }

    // Rebuild in the order the file was read in, so a save diffs cleanly
    // against the backup it came from.
    Json root = Json::object();
    bool wrotePlayers = false, wroteUsers = false, wroteSessions = false;
    for (const std::string& key : topKeyOrder_) {
        if (key == "players") { root["players"] = players; wrotePlayers = true; }
        else if (key == "users") { root["users"] = users; wroteUsers = true; }
        else if (key == "sessions") { root["sessions"] = sessions; wroteSessions = true; }
        else { root[key] = storedTable(key); }
    }
    if (!wrotePlayers) root["players"] = players;
    if (!wroteUsers) root["users"] = users;
    // `sessions` is created lazily, matching the old server: a database that
    // has never issued one does not grow an empty key for it.
    if (!wroteSessions && sessions.size() > 0) root["sessions"] = sessions;
    return root;
}

std::string Database::serialise() const {
    // toJson()'s layout, written straight out. Building that tree copied every
    // inventory in the file twice before dump() walked it a third time, all on
    // the tick; here an untouched row is one append of text it already had.
    std::string out;
    out.reserve(lastSaveBytes_ + lastSaveBytes_ / 16);
    bool firstKey = true;
    const auto key = [&](const std::string& name) {
        out += firstKey ? '{' : ',';
        firstKey = false;
        out += jsonEscape(name);
        out += ':';
    };
    const auto table = [&](const auto& rows, const auto& toJson) {
        if (rows.size() == 0) { out += "{}"; return; }
        char separator = '{';
        for (const std::string& rowKey : rows.keys()) {
            out += separator;
            separator = ',';
            out += jsonEscape(rowKey);
            out += ':';
            out += rows.text(rowKey, toJson);
        }
        out += '}';
    };
    const auto players = [&] { key("players"); table(players_, playerToJson); };
    const auto users = [&] { key("users"); table(users_, accountToJson); };
    const auto sessions = [&] { key("sessions"); table(sessions_, sessionToJson); };

    bool wrotePlayers = false, wroteUsers = false, wroteSessions = false;
    for (const std::string& name : topKeyOrder_) {
        if (name == "players") { players(); wrotePlayers = true; }
        else if (name == "users") { users(); wroteUsers = true; }
        else if (name == "sessions") { sessions(); wroteSessions = true; }
        else {
            auto it = otherTopText_.find(name);
            if (it == otherTopText_.end()) {
                it = otherTopText_.emplace(name, storedTable(name).dump(0)).first;
            }
            key(name);
            out += it->second;
        }
    }
    if (!wrotePlayers) players();
    if (!wroteUsers) users();
    if (!wroteSessions && sessions_.size() > 0) sessions();
    out += '}';
    return out;
}

Json Database::collectDelta() {
    Json delta=Json::object();
    delta["users"]=users_.delta(accountToJson);delta["players"]=players_.delta(playerToJson);
    delta["sessions"]=sessions_.delta(sessionToJson);delta["globals"]=Json::object();
    for(auto it=touchedTop_.begin();it!=touchedTop_.end();) {
        const auto text=storedTable(*it).dump();
        if(committedTop_.count(*it)&&committedTop_[*it]==text){it=touchedTop_.erase(it);continue;}
        delta["globals"][*it]=storedTable(*it);++it;
    }
    return delta;
}

bool Database::setDeltaSink(DeltaSink sink) {
    if(!authoritative()||dirty_||commitPending()||commitUncertain_||!sink)return false;
    serialise();players_.acceptLoadedState();users_.acceptLoadedState();sessions_.acceptLoadedState();
    committedTop_.clear();for(const auto& entry:otherTopText_)committedTop_[entry.first]=entry.second;
    touchedTop_.clear();deltaSink_=std::move(sink);return true;
}

void Database::acknowledgeDelta(const Json& delta) {
    users_.acknowledge(delta["users"],accountToJson);players_.acknowledge(delta["players"],playerToJson);
    sessions_.acknowledge(delta["sessions"],sessionToJson);
    for(const auto& entry:delta["globals"].members()) {
        committedTop_[entry.first]=entry.second.dump();
        if(storedTable(entry.first).dump()==committedTop_[entry.first])touchedTop_.erase(entry.first);
        else touchedTop_.insert(entry.first);
    }
}

bool Database::flushDelta() {
    if(!commitPending()) {
        auto delta=collectDelta();
        if(delta["users"].size()+delta["players"].size()+delta["sessions"].size()+delta["globals"].size()==0) {
            dirty_=false;committedGeneration_=mutationGeneration_;return true;
        }
        if(mutationGeneration_<=committedGeneration_)++mutationGeneration_;
        pendingDelta_=std::move(delta);pendingGeneration_=mutationGeneration_;dirty_=true;
    }
    CommitOutcome outcome=CommitOutcome::Unknown;
    try{outcome=deltaSink_(pendingDelta_);}catch(...){outcome=CommitOutcome::Unknown;}
    if(outcome==CommitOutcome::Pending)return false;
    if(outcome==CommitOutcome::Unknown){commitUncertain_=true;return false;}
    if(outcome==CommitOutcome::Rejected){pendingDelta_=Json();return false;}
    acknowledgeDelta(pendingDelta_);pendingDelta_=Json();
    committedGeneration_=pendingGeneration_;
    const auto remaining=collectDelta();
    dirty_=mutationGeneration_>committedGeneration_||remaining["users"].size()+remaining["players"].size()+
        remaining["sessions"].size()+remaining["globals"].size()>0;
    return true;
}

bool Database::enableBackgroundSave(std::string& errorOut) {
#ifdef FLIX_BACKGROUND_SAVE
    if (snapshotWriter_ || authoritative() || loadFailed_ || path_.empty()) {
        errorOut = "background save requires a loaded independent database";
        return false;
    }
    snapshotWriter_ = std::make_unique<SnapshotWriter>(path_);
    users_.trackSnapshotRows();
    players_.trackSnapshotRows();
    sessions_.trackSnapshotRows();
    otherTop_.trackSnapshotRows();
    queueSnapshot();
    return true;
#else
    errorOut = "background save requires a threaded server";
    return false;
#endif
}

void Database::queueSnapshot(bool finalSave) {
#ifdef FLIX_BACKGROUND_SAVE
    const auto started = std::chrono::steady_clock::now();
    // A checkpoint preserves the legacy observation times for holders of
    // Hyper items, without inspecting every offline inventory on each tick.
    auto inspect = snapshotHyperHolders_;
    inspect.insert(players_.touchedKeys().begin(), players_.touchedKeys().end());
    const double observedAt = static_cast<double>(nowMillis());
    for (const auto& id : inspect) {
        const auto* record = std::as_const(players_).find(id);
        if (record) {
            const auto holdings = currentHyperHoldings(*record, observedAt);
            if (std::any_of(holdings.begin(), holdings.end(), [](const auto& entry) { return entry.second > 0; })) {
                rememberHyperHoldings(*players_.find(id), observedAt);
                snapshotHyperHolders_.insert(id);
                continue;
            }
        }
        snapshotHyperHolders_.erase(id);
    }
    SnapshotWriter::Job job;
    job.topOrder = topKeyOrder_;
    job.generation = ++mutationGeneration_;
    job.finalSave = finalSave;
    users_.takeSnapshotChanges(job.users.rows, job.users.order);
    players_.takeSnapshotChanges(job.players.rows, job.players.order);
    sessions_.takeSnapshotChanges(job.sessions.rows, job.sessions.order);
    otherTop_.takeSnapshotChanges(job.globals.rows, job.globals.order);
    dirty_ = true;
    snapshotWriter_->requested = false;
    const auto generation = job.generation;
    const auto rows = job.users.rows.size() + job.players.rows.size() +
                      job.sessions.rows.size() + job.globals.rows.size();
    snapshotWriter_->submit(std::move(job));
    const double millis = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("[persist-handoff] generation=%llu rows=%zu ms=%.1f\n",
                static_cast<unsigned long long>(generation), rows, millis);
#else
    (void)finalSave;
#endif
}

void Database::requestBackgroundSave() {
#ifdef FLIX_BACKGROUND_SAVE
    if (snapshotWriter_) snapshotWriter_->requested = true;
#endif
}

void Database::collectSnapshotResult(bool wait) {
#ifdef FLIX_BACKGROUND_SAVE
    auto result = snapshotWriter_->takeResult(wait);
    if (!result) return;
    auto& writer = *snapshotWriter_;
    if (result->outcome == SnapshotOutcome::Saved) {
        committedGeneration_ = result->generation;
        dirty_ = mutationGeneration_ != committedGeneration_;
        writer.retryAt = {};
        std::printf("[persist-worker] saved generation=%llu bytes=%zu ms=%.1f\n",
                    static_cast<unsigned long long>(result->generation), result->bytes, result->millis);
    } else {
        commitUncertain_ = result->outcome == SnapshotOutcome::Uncertain;
        dirty_ = true;
        writer.requested = true;
        writer.retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        std::fprintf(stderr, "[persist-worker] %s generation=%llu ms=%.1f\n",
                     commitUncertain_ ? "rename durability unconfirmed" : "write failed",
                     static_cast<unsigned long long>(result->generation), result->millis);
    }
    writer.latest = std::move(result);
#else
    (void)wait;
#endif
}

void Database::pollBackgroundSave() {
#ifdef FLIX_BACKGROUND_SAVE
    if (!snapshotWriter_ || snapshotWriter_->writing()) return;
    collectSnapshotResult(false);
    if (!commitUncertain_ && snapshotWriter_->requested &&
        std::chrono::steady_clock::now() >= snapshotWriter_->retryAt) queueSnapshot();
#endif
}

bool Database::saveBackgroundNow(bool finalSave) {
#ifdef FLIX_BACKGROUND_SAVE
    collectSnapshotResult(true);
    if (commitUncertain_) return false;
    queueSnapshot(finalSave);
    collectSnapshotResult(true);
    return !dirty_ && !commitUncertain_;
#else
    (void)finalSave;
    return false;
#endif
}

bool Database::finishBackgroundSave() {
#ifdef FLIX_BACKGROUND_SAVE
    if (!snapshotWriter_ || !saveBackgroundNow(true)) return false;
    const auto& result = *snapshotWriter_->latest;
    std::printf("[persist-shutdown] generation=%llu accounts=%zu players=%zu bytes=%zu sha256=%s\n",
                static_cast<unsigned long long>(result.generation), result.accounts, result.players,
                result.bytes, result.sha.c_str());
    return true;
#else
    return false;
#endif
}

bool Database::save() {
    // The whole point of the flag. The in-memory database is the empty
    // default when a load fails, and writing that out turns a file an operator
    // can still repair into total, permanent account loss.
    if (loadFailed_ || commitUncertain_ || (!authoritative() && path_.empty())) return false;
    if (backgroundSaveEnabled()) return saveBackgroundNow();
    if(deltaSink_&&commitPending())return flushDelta();

    // A mutable Table::find invalidates that player's cached JSON. Inspect
    // through the const table first: most accounts own no H, so rebuilding
    // every offline inventory here turned each autosave into a server stall.
    const auto& observedPlayers = static_cast<const decltype(players_)&>(players_);
    const double observedAt = static_cast<double>(nowMillis());
    // Shared readers contain foreign accounts too. Only records actually
    // changed by this process may acquire a new observation timestamp.
    const auto historyIds=authoritative()?players_.delta(playerToJson).keys():players_.keys();
    for (const auto& id : historyIds) {
        const auto* observed=observedPlayers.find(id);if(!observed)continue;
        const auto holdings = currentHyperHoldings(*observed, observedAt);
        if (std::any_of(holdings.begin(), holdings.end(), [](const auto& entry) { return entry.second > 0; }))
            rememberHyperHoldings(*players_.find(id), observedAt);
    }
    if(deltaSink_)return flushDelta();
    const std::string text = serialise();
    lastSaveBytes_ = text.size();
    if (authoritative()) {
        CommitOutcome outcome = CommitOutcome::Unknown;
        try { outcome = commitSink_(text); }
        catch (...) { outcome = CommitOutcome::Unknown; }
        if (outcome == CommitOutcome::Unknown) commitUncertain_ = true;
        if (outcome != CommitOutcome::Committed) return false;
        dirty_ = false;
        committedGeneration_=mutationGeneration_;
        return true;
    }
    const std::string tmp = path_ + ".tmp";

    std::FILE* file = std::fopen(tmp.c_str(), "wb");
    if (!file) return false;
    const std::size_t written = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), file);
    const bool flushed = std::fflush(file) == 0;
    if (flushed) ::fsync(::fileno(file));
    const bool closed = std::fclose(file) == 0;
    if (written != text.size() || !flushed || !closed) {
        std::remove(tmp.c_str());
        return false;
    }

    // Verify before promoting. The failure this actually guards against is
    // truncation -- the process dying mid-write, or a full disk -- and a
    // truncated file is a SHORT one, which a length compare catches for free.
    // Re-parsing the file instead would cost megabytes of work per save to
    // catch a corruption mode a healthy filesystem does not produce.
    struct stat info {};
    if (::stat(tmp.c_str(), &info) != 0 ||
        static_cast<std::size_t>(info.st_size) != text.size()) {
        std::remove(tmp.c_str());
        return false;
    }

    // rename(2) is atomic within a filesystem: either the old file or the new
    // one is there, never a half of either.
    if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    dirty_ = false;
    return true;
}

std::string Database::backupDirectoryFor(const std::string& databasePath) {
    // One level above the runtime directory, which is what puts snapshots
    // outside dist/ and therefore out of reach of a redeploy that replaces it.
    const std::size_t slash = databasePath.find_last_of('/');
    // A bare `inventory.json`: the working directory IS the level above.
    if (slash == std::string::npos) return "db_backups";
    // A database at the very root ("/inventory.json") has no level above.
    if (slash == 0) return "/db_backups";
    const std::string directory = databasePath.substr(0, slash);
    const std::size_t parent = directory.find_last_of('/');
    // A single relative component ("dist/inventory.json"), so again the
    // working directory.
    if (parent == std::string::npos) return "db_backups";
    // A single ABSOLUTE component ("/tmp/inventory.json"). The level above is
    // the filesystem root, which is not a place anything writes; the database's
    // own directory is as far up as this can usefully go.
    if (parent == 0) return directory + "/db_backups";
    return directory.substr(0, parent) + "/db_backups";
}

bool Database::isOwnSnapshotName(const std::string& name) {
    // inventory-YYYY-MM-DDTHH-MM-SS-mmmZ-<label>.snapshot.json, exactly as
    // backup() spells it below: the time in one fixed shape, then a label of
    // one to forty of [a-z0-9_-]. Strict on purpose. A pattern that merely
    // started and ended right would also take a copy an operator named to
    // look like one, and the cost of a miss is only a file nobody prunes.
    static const std::string prefix = "inventory-";
    static const std::string suffix = kSnapshotSuffix;
    static const char shape[] = "dddd-dd-ddTdd-dd-dd-dddZ-";
    const std::size_t stampLength = sizeof shape - 1;
    if (name.size() <= prefix.size() + stampLength + suffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    for (std::size_t i = 0; i < stampLength; ++i) {
        const char c = name[prefix.size() + i];
        if (shape[i] == 'd' ? !(c >= '0' && c <= '9') : c != shape[i]) return false;
    }
    const std::size_t labelStart = prefix.size() + stampLength;
    const std::size_t labelLength = name.size() - suffix.size() - labelStart;
    if (labelLength == 0 || labelLength > 40) return false;
    for (std::size_t i = labelStart; i < labelStart + labelLength; ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool Database::backup(const std::string& label, BackupInfo& out, std::string& errorOut) {
    // Same latch as save(): the in-memory database is the empty default when a
    // load fails, and a "backup" of that is a file that looks like a rescue and
    // is total account loss.
    if (loadFailed_) {
        errorOut = "the database failed to load; refusing to snapshot an empty one";
        return false;
    }

    std::string safeLabel;
    for (const char c : lowerCase(label)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok) safeLabel.push_back(c);
        else if (!safeLabel.empty() && safeLabel.back() != '-') safeLabel.push_back('-');
        if (safeLabel.size() >= 40) break;
    }
    while (!safeLabel.empty() && safeLabel.back() == '-') safeLabel.pop_back();
    if (safeLabel.empty()) safeLabel = "manual";

    const std::string directory = backupDirectory();
    if (::mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
        errorOut = "could not create " + directory;
        return false;
    }

    // The reference's `new Date().toISOString()` with ':' and '.' replaced,
    // because a colon is not a filename character everywhere this runs.
    const std::int64_t millis = nowMillis();
    const std::int64_t days = millis / 86400000 - (millis % 86400000 < 0 ? 1 : 0);
    std::int64_t rest = millis - days * 86400000;
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%sT%02d-%02d-%02d-%03dZ", civilFromDays(days).c_str(),
                  static_cast<int>(rest / 3600000), static_cast<int>(rest / 60000 % 60),
                  static_cast<int>(rest / 1000 % 60), static_cast<int>(rest % 1000));

    const std::string file =
        directory + "/inventory-" + stamp + "-" + safeLabel + kSnapshotSuffix;
    // Pretty-printed, as the reference writes it: a backup is read by a person
    // trying to rescue an account, not by the loader's hot path.
    const std::string text = toJson().dump(2);

    std::FILE* handle = std::fopen(file.c_str(), "wb");
    if (handle == nullptr) {
        errorOut = "could not open " + file;
        return false;
    }
    const std::size_t written = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), handle);
    const bool flushed = std::fflush(handle) == 0;
    if (flushed) ::fsync(::fileno(handle));
    const bool closed = std::fclose(handle) == 0;

    struct stat info {};
    if (written != text.size() || !flushed || !closed || ::stat(file.c_str(), &info) != 0 ||
        static_cast<std::size_t>(info.st_size) != text.size()) {
        std::remove(file.c_str());
        errorOut = "the snapshot did not write in full";
        return false;
    }

#ifdef __EMSCRIPTEN__
    // Closed, not open, when the host cannot vouch for it. Every check above
    // passes on MEMFS, which is where a directory nothing mounted lands -- and
    // until server/main.cpp mounted this one, that was every Node server's
    // db_backups: `backup_db` and `update`'s mandatory first step reported a
    // real-looking path, and the snapshot went with the restart it was taken
    // for. A refusal here is what stops `update` going ahead on one. The
    // offline page has no host at all: only its database file is mirrored to
    // browser storage, so a snapshot there would be gone at the next reload,
    // and it is refused in the same way, in words that fit.
    const double onHost = hostFileSize(file);
    if (onHost != static_cast<double>(text.size())) {
        std::remove(file.c_str());
        errorOut = onHost == -2
                       ? std::string("this build keeps its database in browser storage and has no "
                                     "disk to keep a snapshot on")
                       : "the snapshot did not reach the host's disk (" + directory +
                             " is not mounted from it), so it would not survive a restart";
        return false;
    }
#endif

    out.file = file;
    out.bytes = text.size();
    out.modifiedMillis = millis;

    // Pruning is best-effort: it must never turn a good backup into a failure.
    // And it reaches only this server's own snapshots, by name: the directory
    // also holds the TypeScript server's, under the old pattern, and whatever
    // an operator keeps there, and neither is this server's to delete. Those
    // are listed and left alone, so they do not count towards the thirty.
    std::vector<BackupInfo> own;
    for (BackupInfo& info : listBackups()) {
        const std::size_t slash = info.file.find_last_of('/');
        if (isOwnSnapshotName(slash == std::string::npos ? info.file
                                                          : info.file.substr(slash + 1))) {
            own.push_back(std::move(info));
        }
    }
    for (std::size_t i = kMaxDatabaseBackups; i < own.size(); ++i) {
        std::remove(own[i].file.c_str());
    }
    return true;
}

std::vector<Database::BackupInfo> Database::listBackups() const {
    std::vector<BackupInfo> out;
    const std::string directory = backupDirectory();
    DIR* dir = ::opendir(directory.c_str());
    if (dir == nullptr) return out;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("inventory-", 0) != 0) continue;
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        const std::string full = directory + "/" + name;
        struct stat info {};
        if (::stat(full.c_str(), &info) != 0) continue;
        out.push_back({full, static_cast<std::size_t>(info.st_size),
                       static_cast<std::int64_t>(info.st_mtime) * 1000});
    }
    ::closedir(dir);
    // Newest first, which is the order somebody looking for the last good one
    // wants to read.
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) {
        if (a.modifiedMillis != b.modifiedMillis) return a.modifiedMillis > b.modifiedMillis;
        return a.file > b.file;
    });
    return out;
}

bool Database::maybeSave(std::int64_t nowMillis) {
    if (!dirty_ || loadFailed_ || commitUncertain_ || (!authoritative() && path_.empty())) return false;
    if (everWrote_ && nowMillis - lastWriteMillis_ < kMinWriteIntervalMillis) return false;
    const bool ok = save();
    // The stamp moves even on failure: a disk that is refusing writes must not
    // be retried every tick. The record stays dirty, so the next window tries
    // again and nothing is lost.
    lastWriteMillis_ = nowMillis;
    everWrote_ = true;
    return ok;
}

std::int64_t Database::nowMillis() const {
    if (clock_) return clock_();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------
// Database: accounts
// ---------------------------------------------------------------------------

void Database::indexUser(const std::string& storedKey, const Account& account) {
    // emplace, not assignment: a legacy file may contain both "Bob" and "bob"
    // as separate accounts. Both records are kept -- deleting one on load
    // would be data loss -- and the first spelling wins the lookup. New
    // registrations can no longer create that situation.
    usersByLowerName_.emplace(lowerCase(storedKey), storedKey);
    if (!account.id.empty()) usersById_.emplace(account.id, storedKey);
    if (!account.id.empty() && !account.createdFromHash.empty()) {
        usersByCreator_[account.createdFromHash].push_back(account.id);
    }
}

Account* Database::findUser(const std::string& username) {
    if (username.empty()) return nullptr;
    if (Account* exact = users_.find(username)) return exact;
    auto it = usersByLowerName_.find(lowerCase(username));
    if (it == usersByLowerName_.end()) return nullptr;
    return users_.find(it->second);
}

const Account* Database::findUser(const std::string& username) const {
    // Through the const Table::find, which leaves the row's cached text alone:
    // a read must not cost the next save a re-serialise of the row.
    if (username.empty()) return nullptr;
    if (const Account* exact = users_.find(username)) return exact;
    auto it = usersByLowerName_.find(lowerCase(username));
    if (it == usersByLowerName_.end()) return nullptr;
    return users_.find(it->second);
}

Account* Database::findUserById(const std::string& userId) {
    auto it = usersById_.find(userId);
    if (it == usersById_.end()) return nullptr;
    return users_.find(it->second);
}

const Account* Database::findUserById(const std::string& userId) const {
    auto it = usersById_.find(userId);
    if (it == usersById_.end()) return nullptr;
    return users_.find(it->second);
}

std::string Database::canonicalUsername(const std::string& username) const {
    const Account* account = findUser(username);
    return account ? account->username : std::string();
}

bool Database::validUsername(const std::string& username, std::string& reasonOut) {
    if (username.size() < 3 || username.size() > 16) {
        reasonOut = "Username must be 3 to 16 characters.";
        return false;
    }
    // Names are drawn on nameplates and typed at admin commands, so anything
    // that can be mistaken for a different name -- spaces, combining marks,
    // control characters, right-to-left overrides -- is refused at the door
    // rather than sanitised at every place a name is used.
    for (char c : username) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_';
        if (!allowed) {
            reasonOut = "Username may only contain letters, digits and underscore.";
            return false;
        }
    }
    // Must start with a letter or digit (underscore / symbols still refused).
    // Digit-leading names are allowed; pure symbols are not.
    const char first = username[0];
    const bool letter = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    const bool digit = first >= '0' && first <= '9';
    if (!letter && !digit) {
        reasonOut = "Username must start with a letter or digit.";
        return false;
    }
    return true;
}

bool Database::validPassword(const std::string& password, std::string& reasonOut) {
    // Cutover / florrclone: no minimum length or character-class rules. Empty
    // is still refused; bcrypt's 72-byte cap stays so a longer secret cannot
    // silently match on its prefix alone.
    if (password.empty()) {
        reasonOut = "Password cannot be empty.";
        return false;
    }
    if (password.size() > crypto::kBcryptMaxPasswordBytes) {
        reasonOut = "Password must be at most 72 bytes.";
        return false;
    }
    return true;
}

void Database::setPasswordCost(int cost) {
    passwordCost_ = clamp(cost, crypto::kBcryptMinCost, crypto::kBcryptMaxCost);
}

std::string Database::newUserId() {
    static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::uint8_t raw[12];
        crypto::secureRandom().bytes(raw, sizeof(raw));
        std::string id;
        id.reserve(sizeof(raw));
        // The modulo bias is real and irrelevant: an id has to be unique, not
        // uniform, and 12 base-36 characters is ~62 bits either way.
        for (std::uint8_t byte : raw) id.push_back(kAlphabet[byte % 36]);
        if (usersById_.find(id) == usersById_.end()) return id;
    }
    return {};
}

std::string Database::serverSecret() {
    std::string salt = storedTable("ipSalt").asString();
    if (salt.empty()) {
        salt = crypto::secureRandom().hex(16);
        otherTop_.insert("ipSalt") = Json(salt);
        if (authoritative()) touchedTop_.insert("ipSalt");
        otherTopText_.erase("ipSalt");
        if (std::find(topKeyOrder_.begin(), topKeyOrder_.end(), "ipSalt") == topKeyOrder_.end()) {
            topKeyOrder_.push_back("ipSalt");
        }
        markDirty();
    }
    return salt;
}

std::string Database::accountAddressHash(const std::string& addressKey) {
    if (addressKey.empty()) return {};
    const std::string salt = serverSecret();
    // Same construction the TypeScript server's database.accountAddressHash
    // used, so the createdFromHash values it stored still count against the
    // address that made them.
    return crypto::sha256Hex(salt + "|" + addressKey).substr(0, 24);
}

Database::CreatorStanding Database::creatorStanding(const std::string& addressHash,
                                                    std::int64_t idleWindowMillis,
                                                    std::int64_t standingWindowMillis) const {
    CreatorStanding standing;
    if (addressHash.empty()) return standing;
    const auto made = usersByCreator_.find(addressHash);
    if (made == usersByCreator_.end()) return standing;
    const std::int64_t now = nowMillis();
    for (auto it = made->second.rbegin(); it != made->second.rend(); ++it) {
        const Account* account = findUserById(*it);
        // An account the editor has since restamped is not this address's.
        if (account == nullptr || account->createdFromHash != addressHash) continue;
        if (account->createdAtMillis < now - standingWindowMillis) break;
        const PlayerRecord* record = findProgress(*it);
        if (record != nullptr && !isStarterProgress(*record)) ++standing.played;
        else if (account->createdAtMillis >= now - idleWindowMillis) ++standing.idleToday;
    }
    return standing;
}

namespace {

/// BJ akuah only (`-DFLIX_DEFAULT_NEW_ACCOUNTS_ADMIN=ON`). Kept out of
/// createAccount's body so HK .161 source greps for `admin = true` stay clean.
void applyNewAccountPrivileges(Account& account) {
#ifdef FLIX_DEFAULT_NEW_ACCOUNTS_ADMIN
    account.admin = true;
    account.adminTier = kTopAdminTier;
#else
    (void)account;
#endif
}

} // namespace

CreateResult Database::createUser(const std::string& username, const std::string& password,
                                  const std::string& createdFromHash) {
    return createAccount(username, password, createdFromHash, passwordCost_);
}

CreateResult Database::createAccount(const std::string& username, const std::string& password,
                                     const std::string& createdFromHash, int cost) {
    CreateResult result;
    if (!validUsername(username, result.reason)) {
        result.status = CreateStatus::UsernameInvalid;
        return result;
    }
    if (!validPassword(password, result.reason)) {
        result.status = CreateStatus::PasswordInvalid;
        return result;
    }
    if (findUser(username) != nullptr) {
        result.status = CreateStatus::UsernameTaken;
        result.reason = "that name is already registered";
        return result;
    }
    const std::string id = newUserId();
    if (id.empty()) {
        result.status = CreateStatus::ServerError;
        result.reason = "could not allocate an account id";
        return result;
    }

    Account& account = users_.insert(username);
    account.id = id;
    account.username = username;
    account.passwordHash = crypto::bcryptHash(password, cost);
    // Default: no admin. Promote via inventory / admin DB editor when needed
    // (HK .161 continuous target is a normal no-admin server).
    // BJ akuah builds call applyNewAccountPrivileges via the compile flag.
    applyNewAccountPrivileges(account);
    account.createdAtMillis = nowMillis();
    account.createdFromHash = createdFromHash;
    indexUser(username, account);
    // Give the account its (empty) progress row now, so `players` and `users`
    // never disagree about who exists.
    players_.insert(id);
    markDirty();

    result.account = &account;
    return result;
}

bool Database::isGuestUsername(const std::string& username) {
    if (username.size() != 12 || username.compare(0, 4, "User") != 0) return false;
    for (std::size_t i = 4; i < username.size(); ++i) {
        if (username[i] < '0' || username[i] > '9') return false;
    }
    return true;
}

CreateResult Database::createGuest(const std::string& createdFromHash) {
    CreateResult result;
    // Eight digits, zero-padded: the browser build's `User\d{8}`, which is the
    // shape `/admin delete_guests` sweeps. A collision is a retry, not a
    // refusal -- a hundred million names is room enough that a few draws
    // always land, and the loop is bounded so a full namespace cannot hang
    // the tick.
    std::string username;
    for (int attempt = 0; attempt < 64 && username.empty(); ++attempt) {
        std::uint8_t raw[4];
        crypto::secureRandom().bytes(raw, sizeof(raw));
        const std::uint32_t draw = (static_cast<std::uint32_t>(raw[0]) << 24) |
                                   (static_cast<std::uint32_t>(raw[1]) << 16) |
                                   (static_cast<std::uint32_t>(raw[2]) << 8) | raw[3];
        char digits[9];
        std::snprintf(digits, sizeof digits, "%08u", static_cast<unsigned>(draw % 100000000u));
        const std::string candidate = std::string("User") + digits;
        if (findUser(candidate) == nullptr) username = candidate;
    }
    if (username.empty()) {
        result.status = CreateStatus::ServerError;
        result.reason = "could not find a free guest name";
        return result;
    }

    // 32 random bytes, hex: a password with nobody to type it, so its only job
    // is to be unguessable. It passes validPassword (64 printable bytes) and is
    // then forgotten -- createUser hashes it and nothing keeps the text.
    result = createAccount(username, crypto::secureRandom().hex(32), createdFromHash,
                           crypto::kBcryptMinCost);
    if (result.ok() && result.account != nullptr) {
        result.account->guest = true;
        result.account->passwordChosen = false;
    }
    return result;
}

bool Database::renameUser(const std::string& userId, const std::string& username,
                          std::string& reasonOut) {
    auto stored = usersById_.find(userId);
    if (stored == usersById_.end()) {
        reasonOut = "That account no longer exists.";
        return false;
    }
    const std::string oldKey = stored->second;
    if (!validUsername(username, reasonOut)) return false;
    if (isGuestUsername(username)) {
        reasonOut = "That name is kept for guest accounts. Pick another.";
        return false;
    }
    // Any other account, in any case: "Bob" and "bob" side by side is the
    // impersonation findUser's case-blindness exists to stop. The account's
    // own row does not count -- but a guest renaming itself to its own name is
    // refused above anyway, by the guest shape.
    const Account* holder = std::as_const(*this).findUser(username);
    if (holder != nullptr && holder->id != userId) {
        reasonOut = "That username is taken.";
        return false;
    }

    if (!users_.rekey(oldKey, username)) {
        reasonOut = "That username is taken.";
        return false;
    }
    Account* account = users_.find(username);
    account->username = username;
    account->guest = false;
    // Only when the lower-case entry was this row's: a legacy file can hold
    // "Bob" and "bob" as two accounts, and the other one owns the lookup then.
    const auto lower = usersByLowerName_.find(lowerCase(oldKey));
    if (lower != usersByLowerName_.end() && lower->second == oldKey) {
        usersByLowerName_.erase(lower);
    }
    usersByLowerName_[lowerCase(username)] = username;
    usersById_[userId] = username;

    // Every token the account holds names it, and resolveSession drops one
    // whose name no longer finds the account -- so without this, picking a
    // name would sign the player out everywhere at their next resume.
    for (const std::string& tokenHash : sessions_.keys()) {
        const Session* session = std::as_const(sessions_).find(tokenHash);
        if (session == nullptr || session->userId != userId) continue;
        sessions_.find(tokenHash)->username = username;
    }
    markDirty();
    return true;
}

bool Database::eraseUser(const std::string& username) {
    // Resolve through findUser so any spelling works, then delete by the key
    // the account is actually STORED under -- a legacy file may hold both
    // "Bob" and "bob", and erasing the typed spelling would take the wrong one
    // or nothing at all.
    const Account* account = findUser(username);
    if (account == nullptr) return false;
    const std::string storedKey = account->username;
    const std::string userId = account->id;

    // Sessions first, while the account is still there to match against: a
    // token outliving its account resolves to nothing anyway, and leaving the
    // orphans in the file only means writing rubbish that the next load has to
    // step over.
    std::vector<std::string> doomedTokens;
    for (const std::string& tokenHash : sessions_.keys()) {
        const Session* session = std::as_const(sessions_).find(tokenHash);
        if (session != nullptr && session->userId == userId) doomedTokens.push_back(tokenHash);
    }
    for (const std::string& tokenHash : doomedTokens) sessions_.erase(tokenHash);

    if (const auto made = usersByCreator_.find(account->createdFromHash);
        made != usersByCreator_.end()) {
        auto& ids = made->second;
        ids.erase(std::remove(ids.begin(), ids.end(), userId), ids.end());
        if (ids.empty()) usersByCreator_.erase(made);
    }
    users_.erase(storedKey);
    if (!userId.empty()) players_.erase(userId);
    usersByLowerName_.erase(lowerCase(storedKey));
    if (!userId.empty()) usersById_.erase(userId);
    markDirty();
    return true;
}

bool Database::isStarterProgress(const PlayerRecord& record) {
    if (record.totalXp > 0) return false;
    // A skin is account content somebody chose; it outranks the sweep.
    if (record.renderFlags != 0) return false;
    if (!record.equippedSkinId.empty()) return false;

    // The starter inventory is exactly {common: {petal_basic: 5}}. Anything
    // else -- a second rarity, a second petal, or a different count -- means
    // this account picked something up.
    if (record.inventory.isObject() && !record.inventory.keys().empty()) {
        if (record.inventory.keys().size() > 1) return false;
        const std::string& rarity = record.inventory.keys().front();
        if (rarity != "common") return false;
        const Json& items = record.inventory[rarity];
        if (!items.isObject()) return false;
        if (items.keys().size() > 1) return false;
        if (items.keys().size() == 1) {
            const std::string& key = items.keys().front();
            if (key != "petal_basic" || PlayerRecord::stackCount(items[key]) != 5) return false;
        }
    }

    for (const std::optional<StoredItem>& slot : record.loadout) {
        if (!slot.has_value()) continue;
        if (slot->petalType != "basic" || slot->rarity != Rarity::Common) return false;
    }
    return true;
}

bool Database::verifyPassword(const std::string& username, const std::string& password) {
    Account* account = findUser(username);
    if (!account) return false;

    bool ok = false;
    if (crypto::isBcryptHash(account->passwordHash)) {
        // The stored value's shape decides, not the isPlainText flag: the old
        // server tested `startsWith('$2b$')` and so mistook every $2a$ hash
        // for a plaintext password. A bcrypt hash is never something a player
        // typed, so this branch is always the right one when it matches.
        ok = crypto::bcryptVerify(password, account->passwordHash);
    } else if (!account->passwordSalt.empty()) {
        // florrclone accounts.js: scrypt(password, saltHex, 64) → hex hash.
        ok = crypto::scryptVerifyHex(password, account->passwordSalt, account->passwordHash);
        if (ok) {
            // Upgrade to bcrypt so subsequent logins do not need Node scrypt.
            account->passwordHash = crypto::bcryptHash(password, passwordCost_);
            account->passwordSalt.clear();
            account->isPlainText = false;
        }
    } else {
        ok = crypto::constantTimeEquals(account->passwordHash, password);
        if (ok) {
            // The one moment the plaintext is available. Upgrade in place; the
            // account never stores a bare password again.
            account->passwordHash = crypto::bcryptHash(password, passwordCost_);
            account->isPlainText = false;
        }
    }
    if (!ok) return false;

    account->lastActiveAtMillis = nowMillis();
    markDirty();
    return true;
}

bool Database::passwordMatches(const std::string& username,const std::string& password) const {
    const auto* account=findUser(username);
    if(!account)return false;
    if(crypto::isBcryptHash(account->passwordHash))return crypto::bcryptVerify(password,account->passwordHash);
    if(!account->passwordSalt.empty())return crypto::scryptVerifyHex(password,account->passwordSalt,account->passwordHash);
    return crypto::constantTimeEquals(account->passwordHash,password);
}

bool Database::setPassword(const std::string& username, const std::string& password,
                           std::string& reasonOut) {
    // AshFlor BJ: admins may change passwords in-game like any other account.
    if (!validPassword(password, reasonOut)) return false;

    Account* account = findUser(username);
    if (!account) {
        reasonOut = "no such account";
        return false;
    }

    account->passwordHash = crypto::bcryptHash(password, passwordCost_);
    account->passwordSalt.clear();
    // An account imported with a bare password stops being one here, the same
    // way a successful verify upgrades it: whatever the record was before, it
    // holds a hash now and the flag has to say so.
    account->isPlainText = false;
    // A guest's random password is replaced by one somebody typed -- its owner
    // through ChangePassword, or an admin who will tell them -- so from here a
    // change asks for the current one like any other account's.
    account->passwordChosen = true;
    markDirty();
    return true;
}

// ---------------------------------------------------------------------------
// Database: sessions
// ---------------------------------------------------------------------------

std::string Database::createSession(const std::string& userId, const std::string& username) {
    // 32 bytes: a token has to be unguessable in the whole keyspace, since a
    // guess is a login. Hex rather than base64 so it survives being pasted
    // into a URL, a header, or a support ticket unchanged.
    const std::string token = crypto::secureRandom().hex(32);
    const std::string tokenHash = crypto::sha256Hex(token);
    const std::int64_t now = nowMillis();

    pruneExpiredSessions();

    Session& session = sessions_.insert(tokenHash);
    session.tokenHash = tokenHash;
    session.userId = userId;
    session.username = username;
    session.createdAtMillis = now;
    session.expiresAtMillis = now + kSessionTtlMillis;
    markDirty();
    return token;
}

const Database::Session* Database::resolveSession(const std::string& token,bool touch) {
    if (token.empty()) return nullptr;
    const std::string tokenHash = crypto::sha256Hex(token);
    Session* session = sessions_.find(tokenHash);
    if (!session) return nullptr;

    const std::int64_t now = nowMillis();
    if (session->expiresAtMillis <= now) {
        if(touch){sessions_.erase(tokenHash);markDirty();}
        return nullptr;
    }
    // The account may have been renamed or deleted since the token was issued.
    // A session must never resurrect an account or land on a different one
    // that has since taken the name.
    Account* account = findUser(session->username);
    if (!account || account->id != session->userId) {
        if(touch){sessions_.erase(tokenHash);markDirty();}
        return nullptr;
    }

    // The token is the only key to an account with no chosen password, so it
    // lapses after thirty days of NOT being used rather than thirty days from
    // the day it was issued. An account with a password keeps the fixed term:
    // its owner can always sign in again.
    if(!touch)return session;
    if (!account->passwordChosen) session->expiresAtMillis = now + kSessionTtlMillis;

    account->lastActiveAtMillis = now;
    markDirty();
    return session;
}

void Database::revokeSession(const std::string& token) {
    if (token.empty()) return;
    if (sessions_.erase(crypto::sha256Hex(token))) markDirty();
}

int Database::revokeSessionsForUser(const std::string& username) {
    std::vector<std::string> doomed;
    for (const std::string& tokenHash : sessions_.keys()) {
        const Session* session = std::as_const(sessions_).find(tokenHash);
        if (session && session->username == username) doomed.push_back(tokenHash);
    }
    for (const std::string& tokenHash : doomed) sessions_.erase(tokenHash);
    if (!doomed.empty()) markDirty();
    return static_cast<int>(doomed.size());
}

int Database::pruneExpiredSessions() {
    const std::int64_t now = nowMillis();
    std::vector<std::string> doomed;
    for (const std::string& tokenHash : sessions_.keys()) {
        const Session* session = std::as_const(sessions_).find(tokenHash);
        if (session && session->expiresAtMillis <= now) doomed.push_back(tokenHash);
    }
    for (const std::string& tokenHash : doomed) sessions_.erase(tokenHash);
    if (!doomed.empty()) markDirty();
    return static_cast<int>(doomed.size());
}

// ---------------------------------------------------------------------------
// Database: progress
// ---------------------------------------------------------------------------

PlayerRecord& Database::progress(const std::string& userId) {
    const bool fresh = players_.find(userId) == nullptr;
    PlayerRecord& record = players_.insert(userId);
    if (fresh) markDirty();
    return record;
}

const PlayerRecord* Database::findProgress(const std::string& userId) const {
    return players_.find(userId);
}

int Database::awardWaveStars(const std::string& userId, bool* reachedDailyCap) {
    constexpr int kStarsPerWave = 7500;
    constexpr int kDailyCap = 150000;
    if (reachedDailyCap) *reachedDailyCap = false;
    const std::int64_t today = nowMillis() / 86400000;
    PlayerRecord& record = progress(userId);
    const Json& prior = std::as_const(record.extra)["waveStars"];
    const auto lastDay = static_cast<std::int64_t>(prior["day"].asDouble(-1.0));
    if (lastDay > today) return 0;
    const int earned = lastDay == today ? std::max(0, prior["earned"].asInt()) : 0;
    if (earned >= kDailyCap) {
        if (reachedDailyCap) *reachedDailyCap = true;
        return 0;
    }

    const int award = std::min(kStarsPerWave, kDailyCap - earned);
    auto& ledger = record.extra["waveStars"];
    ledger["day"] = static_cast<double>(today);
    ledger["earned"] = earned + award;
    if (reachedDailyCap) *reachedDailyCap = earned + award >= kDailyCap;
    record.stars += award;
    markDirty();
    return award;
}

bool Database::awardWaveBonusHammer(const std::string& userId) {
    const auto now = nowMillis();
    const auto today = now / 86400000;
    auto& record = progress(userId);
    if (std::as_const(record.extra)["waveBonusHammerDay"].asDouble(-1.0) >= today) return false;
    addHammerLease(record, static_cast<double>(now));
    record.extra["waveBonusHammerDay"] = static_cast<double>(today);
    markDirty();
    return true;
}

DailyStreakResult Database::processDailyStreak(const std::string& userId) {
    constexpr std::int64_t kMillisPerDay = 86400000;
    PlayerRecord& record = progress(userId);

    const std::int64_t now = nowMillis();
    // Floor division: before 1970 the truncating form would round the wrong
    // way and hand out a second reward on the same day.
    const std::int64_t today = (now >= 0 ? now : now - (kMillisPerDay - 1)) / kMillisPerDay;
    const std::int64_t todayMillis = today * kMillisPerDay;
    const std::string todayText = civilFromDays(today);

    DailyStreakResult result;
    result.streak = record.dailyStreak;

    if (record.lastStreakDate != todayText) {
        const std::optional<std::int64_t> last = parseUtcDate(record.lastStreakDate);
        // Exactly one day ago continues the run; anything else -- a gap, a
        // first login, or an unreadable date -- starts a new one.
        result.streak = (last && *last == today - 1) ? record.dailyStreak + 1 : 1;
        result.starsAwarded = dailyStreakReward(result.streak);
        record.dailyStreak = result.streak;
        record.lastStreakDate = todayText;
        record.extra["lastStreakReward"] = result.starsAwarded;
        record.stars += result.starsAwarded;
        result.newDay = true;
        markDirty();
    }

    // An old save can already have a claim from the 1–5 star schedule.
    // Report that actual amount until the first claim under the new schedule.
    result.todayReward = std::as_const(record.extra)["lastStreakReward"].asInt(
        dailyStreakCycleDay(result.streak));
    result.nextClaimAtMillis = todayMillis + kMillisPerDay;
    result.streakExpiresAtMillis = todayMillis + 2 * kMillisPerDay;
    return result;
}

} // namespace flix
