#include "server/hyper_board.h"
#include "server/council.h"
#include "shared/net/web_channel.h"
#include "server/playtime_board.h"
#include "server/shop_lock.h"
#include "server/timed_hammers.h"
#include "server/practice_board.h"
#include "server/daily_drop_bonus.h"
#include "server/game_server.h"
#include "shared/game/assembler.h"
#include "server/hyper_forge.h"
#include "server/crypto.h"
#include "shared/game/hyper_petal_techniques.h"
#include "shared/game/dark_mark.h"

#include "shared/core/process_stats.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_set>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "server/admin_db_key.h"
#include "server/auto_update.h"
#include "server/chat_moderation.h"
#include "server/guilds.h"
#include "server/loot_eligibility.h"
#include "server/text.h"
#include "server/systems/combat.h"
#include "server/systems/loot.h"
#include "server/systems/mob_ai.h"
#include "server/systems/movement.h"
#include "server/systems/petals.h"
#include "server/systems/spawning.h"
#include "server/systems/mode_spawning.h"
#include "server/systems/dungeons.h"
#include "server/systems/npcs.h"
#include "shared/game/absorb.h"
#include "shared/game/prestige.h"
#include "shared/game/chat_images.h"
#include "shared/game/config.h"
#include "shared/game/content_report.h"
#include "shared/game/shop.h"
#include "shared/game/skin_format.h"
#include "shared/game/skin_likeness.h"
#include "shared/game/skills.h"

namespace flix {

#ifdef __EMSCRIPTEN__
EM_JS(double, tickProbeThreadCpuMillis, (), {
    if (typeof process === 'undefined' || typeof process.threadCpuUsage !== 'function') return -1;
    const cpu = process.threadCpuUsage();
    return (cpu.user + cpu.system) / 1000;
});
#else
static double tickProbeThreadCpuMillis() { return -1; }
#endif

static double tickProbeCpuElapsed(double start) {
    return start < 0.0 ? -1.0 : std::max(0.0, tickProbeThreadCpuMillis() - start);
}

double monotonicMillis() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double, std::milli>(clock::now() - start).count();
}

namespace {

std::int64_t pyramidRotationCycle() {
    const auto utcHours = std::chrono::duration_cast<std::chrono::hours>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return utcHours / 4;
}

/// Clamp on the viewport a client may claim, per axis, when nothing it wears
/// pulls the camera out. A client asking to see the whole map is asking for an
/// advantage, and for the server to build it a snapshot of the entire world.
constexpr double kMaxViewportAxis = 2600.0;

/// Headroom on a camera petal's ceiling for the client's own rounding: it
/// reports its window divided by its zoom, rounded, and a design-unit window
/// can come out a unit over 1920.
constexpr double kViewportCeilingSlack = 1.01;

/// The viewport a flower may claim: what its client reported, clamped to what
/// that flower can honestly be drawing.
///
/// The ceiling is not flat, because the camera petals legitimately show more
/// world. Unique antennae draws ten default screens' worth of width, and a flat
/// 2600 clamp streamed and woke barely a quarter of it: mobs were culled in
/// plain view, and the spawn bands stayed empty outside a box around the
/// flower. So it is the default screen divided by the zoom the body's OWN worn
/// loadout grants -- the rule the client draws by (petalCameraZoom) -- and
/// never below the flat allowance every flower has always had. A client still
/// cannot claim more than its loadout pays for.
Vec2 claimableViewport(World& world, Entity body, const ContentRegistry& content, double width,
                       double height) {
    double gogglesScale = 0.0;
    const auto* talents = world.tryGet<PlayerSkillTree>(body);
    double zoom = talents ? 1.0 / (1.0 + talents->skills.antennaeFovBonus()) : 1.0;
    if (const Loadout* loadout = world.tryGet<Loadout>(body)) {
        for (int i = 0; i < (world.has<PlayerTag>(body)&&talents?talents->skills.loadoutSlots():kLoadoutActiveSlots); ++i) {
            // Resolved, as the client resolves it: a mimic beside a third eye
            // pulls the camera out as one at the mimic's tier.
            const EquippedPetal slot = equippedPetal(content, *loadout, i);
            if (slot.empty()) continue;
            if (content.petal(slot.configIndex).id == "paranormal_goggles") {
                const auto* technique = hyperTechniqueInfo("paranormal_goggles", slot.rarity,
                                                           !samePetalTier(slot.reloadRarity, slot.rarity));
                const double scale = hyperAmount(technique, HyperTechnique::GhostSight, 1.25);
                gogglesScale = gogglesScale > 0.0 ? std::min(gogglesScale, scale) : scale;
            }
            zoom = std::min(zoom, equippedCameraZoom(content, slot));
        }
    }
    // Guard against zero or negative zoom before sizing the viewport.
    zoom = std::max(zoom * (gogglesScale > 0.0 ? gogglesScale : 1.0) * kMinimumUserCameraZoom, 0.01);
    const double ceilingX = std::max(kMaxViewportAxis, kViewportWidth / zoom * kViewportCeilingSlack);
    const double ceilingY = std::max(kMaxViewportAxis, kViewportHeight / zoom * kViewportCeilingSlack);
    // A camera axis at the wire maximum may be wider than a uint16 can
    // express. Treat it as a request for the server-authorized ceiling.
    constexpr double saturated = std::numeric_limits<std::uint16_t>::max();
    return {width == saturated ? ceilingX : clamp(width, 320.0, ceilingX),
            height == saturated ? ceilingY : clamp(height, 240.0, ceilingY)};
}

/// How often account progress is written back from the live entity.
constexpr double kPersistIntervalMillis = 30000.0;

/// Ceiling on one simulation step, as a multiple of the nominal one. A long
/// stall must not be paid back as a single giant integration step that walks
/// every flower through a wall.
constexpr double kMaxDeltaSeconds = net::kTickSeconds * 3.0;
/// Low-pass factor on the step, ~a ten-tick time constant.
constexpr double kDeltaSmoothing = 0.1;

/// A tick this long gets its phase split logged. Three nominal ticks: a
/// healthy tick is a few milliseconds, and one this long is a visible hitch.
constexpr double kSlowTickLogMillis = net::kTickMillis * 3.0;
constexpr double kTickCadenceLogMillis = 10000.0;
constexpr std::size_t kSnapshotBacklogLimit = 32 * 1024;
constexpr std::size_t kWorldSnapshotBacklogLimit = 16 * 1024;

/// The batch a craft consumes.
constexpr int kCraftBatch = 5;

/// The closest together two ProfileChecks are each worth a walk of the bag.
/// The shipping client sends one every kProfileCheckMillis; one sooner is
/// dropped rather than paid for.
constexpr double kProfileCheckFloorMillis = net::kProfileCheckMillis * 0.5;
constexpr double kStaleProfileRepairMillis = 4 * net::kProfileCheckMillis;

// The database stores petals by NAME, because that is the shape real accounts
// are already saved in; the wire uses a dense index, because a name in every
// snapshot would cost more than the whole rest of the message. These two
// functions are the only place the two spellings meet.
//
// Inventory keys additionally carry a "petal_" prefix that loadout entries do
// not. That is the old schema's one real wart, and it is load-bearing: change
// either side and every stored loadout is orphaned.

std::string inventoryKey(std::uint16_t petalIndex) {
    return "petal_" + content().petal(petalIndex).id;
}

} // namespace

// Outside the file's own namespace, and declared in game_server.h, because the
// dashboard's view of a bag (server/admin_dashboard.cpp) reads these keys too.
std::uint16_t petalIndexFromInventoryKey(const std::string& key) {
    const std::string id = key.rfind("petal_", 0) == 0 ? key.substr(6) : key;
    return content().petalIndex(id);
}

namespace {

/// Takes `count` from the inventory, or nothing at all when short. Never
/// partially succeeds: a craft that consumed three of the five it needed and
/// then failed would quietly destroy them.
bool takeFromInventory(PlayerRecord& record, std::uint16_t petalIndex, Rarity rarity, int count) {
    if (petalIndex == kNoPetal || count <= 0) return false;
    const std::string key = inventoryKey(petalIndex);
    if (record.itemCount(rarity, key) < count) return false;
    record.addItem(rarity, key, -count);
    return true;
}

void giveToInventory(PlayerRecord& record, std::uint16_t petalIndex, Rarity rarity, int count) {
    if (petalIndex == kNoPetal || count <= 0) return;
    record.addItem(rarity, inventoryKey(petalIndex), count);
}

/// Every stack of the bag exactly as the Profile carries it, as
/// `visit(petalIndex, rarity, count)`. The inventory is a sparse dictionary of
/// rarity -> item name -> count. Anything whose name this build does not know
/// (an item from a newer server, or a non-petal) is skipped for the wire but
/// left untouched in the record, so it survives the next save. sendProfile
/// writes what this visits and profileDigest folds it, so the bag a client is
/// checked against is the bag it would be sent. `only` skips every tier that
/// is not it: the walk a ProfileDelta makes for one group, which has to see
/// exactly what the Profile's walk sees there.
template <typename Visit>
void forEachProfileStack(const PlayerRecord& record, Visit&& visit,
                         std::optional<Rarity> only = std::nullopt) {
    for (const std::string& rarityName : record.inventory.keys()) {
        const Rarity rarity = parseRarity(rarityName);
        if (only && rarity != *only) continue;
        const Json& byType = record.inventory[rarityName];
        for (const std::string& key : byType.keys()) {
            const auto count = static_cast<std::uint32_t>(PlayerRecord::stackCount(byType[key]));
            if (count == 0) continue;
            const std::uint16_t index = petalIndexFromInventoryKey(key);
            if (index == kInvalidIndex) continue;
            visit(index, rarity, count);
        }
    }
}

/// Every cell of the kill ledger exactly as the Profile carries it, as
/// `visit(mobIndex, rarity, count)`, in the order it is written: two entries
/// that land on one cell leave the client holding the later one. sendProfile
/// writes what this visits and a ProfileDelta's cells are read through it.
template <typename Visit>
void forEachProfileKill(const PlayerRecord& record, Visit&& visit) {
    for (const std::string& mobId : record.mobKills.keys()) {
        const std::uint16_t index = content().mobIndex(mobId);
        if (index == kInvalidIndex) continue;
        const Json& byTier = record.mobKills[mobId];
        if (!byTier.isObject()) continue;
        for (const std::string& tier : byTier.keys()) {
            const std::uint32_t count = static_cast<std::uint32_t>(std::max(0, byTier[tier].asInt()));
            if (count == 0) continue;
            visit(index, parseRarity(tier), count);
        }
    }
}

/// Lists `cell` among `due` once.
void noteDue(std::vector<Session::ProfileCell>& due, std::uint16_t index, Rarity rarity) {
    const Session::ProfileCell cell{index, rarity};
    if (std::find(due.begin(), due.end(), cell) == due.end()) due.push_back(cell);
}

/// Whether the account holds `petalIndex` at universal anywhere: in the bag
/// or worn in a loadout slot. One of each petal may exist at universal, and
/// both are where it can be.
bool holdsUniversal(const PlayerRecord& record, std::uint16_t petalIndex) {
    if (petalIndex == kNoPetal) return false;
    if (record.itemCount(Rarity::Universal, inventoryKey(petalIndex)) > 0) return true;
    const std::string& id = content().petal(petalIndex).id;
    for (const std::optional<StoredItem>& slot : record.loadout) {
        if (slot.has_value() && slot->rarity == Rarity::Universal && slot->petalType == id) {
            return true;
        }
    }
    return false;
}

/// Rng as a standard bit generator, which is all the <random> distributions
/// ask of one.
struct RngBits {
    Rng& rng;
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return ~result_type{0}; }
    result_type operator()() { return rng.next(); }
};

/// How many of `trials` independent rolls at `chance` come up. Rolled one by
/// one while that is cheap; past that, one draw from the binomial they add up
/// to, which is the same number by a different road -- and a single draw
/// whether the trials are a hundred or four hundred million.
std::int64_t countSuccesses(Rng& rng, std::int64_t trials, double chance) {
    if (trials <= 0 || chance <= 0.0) return 0;
    if (chance >= 1.0) return trials;
    constexpr std::int64_t kRollOneByOne = 64;
    if (trials <= kRollOneByOne) {
        std::int64_t hits = 0;
        for (std::int64_t i = 0; i < trials; ++i) {
            if (rng.chance(chance)) ++hits;
        }
        return hits;
    }
    RngBits bits{rng};
    return std::binomial_distribution<std::int64_t>(trials, chance)(bits);
}

/// A loadout slot as a preset remembers it: which petal, at which rarity.
/// None of the per-instance state a worn slot can carry.
StoredItem presetItem(const std::string& petalType, Rarity rarity) {
    StoredItem item;
    item.type = "petal";
    item.petalType = petalType;
    item.rarity = rarity;
    return item;
}

void savePresetSkills(PlayerRecord& record, const std::string& name) {
    Json skills = Json::object();
    for (int i = 0; i < kSkillCount; ++i) {
        const auto skill = static_cast<SkillId>(i);
        if (skill == SkillId::Prestige) continue;
        const int level = record.skills.florrLevel(skill);
        if (level > 0) skills[kSkillKeys[i]] = level;
    }
    record.extra["loadoutPresetSkills"][name] = std::move(skills);
}

void loadPresetSkills(PlayerRecord& record, const std::string& name) {
    const Json& extra = record.extra;
    const Json& saved = extra["loadoutPresetSkills"][name];
    if (!saved.isObject()) return; // Older presets start with the current build.
    SkillSet skills;
    // Prestige grants access to maps. It belongs to the account and changing
    // equipment cannot revoke the session's permission to remain there.
    skills.set(SkillId::Prestige, record.skills.level(SkillId::Prestige));
    for (int i = 0; i < kSkillCount; ++i) {
        const auto skill = static_cast<SkillId>(i);
        if (skill == SkillId::Prestige) continue;
        skills.setFlorrLevel(skill, std::clamp(saved[kSkillKeys[i]].asInt(0), 0, skillTierCount(skill)));
    }
    for (const auto& gate : kSkillGates)
        if (!skills.prerequisiteMet(gate.child)) skills.set(gate.child, -1);
    if (skills.spent() <= record.skills.spent() + record.talentPoints()) record.skills = skills;
}

void autoSaveActivePreset(PlayerRecord& record) {
    const int active=record.extra["activeLoadoutPreset"].asInt(-1);
    if(active<0||active>=kLoadoutPresetCount)return;
    std::vector<std::optional<StoredItem>> slots(kLoadoutSlots);
    for(std::size_t i=0;i<slots.size()&&i<record.loadout.size();++i)
        if(record.loadout[i])slots[i]=presetItem(record.loadout[i]->petalType,record.loadout[i]->rarity);
    const std::string name = loadoutPresetName(active);
    record.loadoutPresets[name] = std::move(slots);
    savePresetSkills(record, name);
}

/// How closely a preset load came to what was saved.
struct PresetLoad {
    int downgraded = 0;   ///< equipped, but at a lower rarity than was saved
    int missing = 0;      ///< not owned at the saved rarity or any below it
};

/// Re-equips a saved preset onto `record`.
///
/// Everything worn goes back into the bag first, so each slot draws from the
/// whole collection: a petal the preset wants in slot 0 may be sitting in
/// slot 7 right now. Then two passes. The first seats every petal that is
/// there at its saved rarity; only then does the second step what is left
/// down a rarity at a time. In one pass, a slot early on the bar that had to
/// step down could take the very petal a later slot was saved with, and the
/// later slot would step down too. A petal owned at no rarity at or below its
/// saved one leaves its slot empty.
PresetLoad equipPreset(PlayerRecord& record, const std::vector<std::optional<StoredItem>>& preset, int unlockedSlots=10) {
    if (record.loadout.size() < kLoadoutSlots) record.loadout.resize(kLoadoutSlots);
    const std::vector<std::optional<StoredItem>> previous = record.loadout;
    for (const std::optional<StoredItem>& worn : previous) {
        // Under its raw key, not via the index: a petal this build no longer
        // knows still belongs to the account, and the bag keeps unknown keys.
        if (worn && !worn->petalType.empty()) {
            record.addItem(worn->rarity, "petal_" + worn->petalType, 1);
        }
    }

    std::vector<std::optional<StoredItem>> next(kLoadoutSlots);
    std::vector<std::size_t> unseated;
    PresetLoad result;
    const std::size_t slots = std::min<std::size_t>(preset.size(), kLoadoutSlots);
    for (std::size_t i = 0; i < slots; ++i) {
        if (!preset[i] || static_cast<int>(i%kLoadoutActiveSlots)>=unlockedSlots) continue;
        const std::uint16_t index = content().petalIndex(preset[i]->petalType);
        if (index == kInvalidIndex) {
            ++result.missing;
        } else if (takeFromInventory(record, index, preset[i]->rarity, 1)) {
            next[i] = presetItem(preset[i]->petalType, preset[i]->rarity);
        } else {
            unseated.push_back(i);
        }
    }
    for (const std::size_t i : unseated) {
        const std::uint16_t index = content().petalIndex(preset[i]->petalType);
        for (int tier = petalTier(preset[i]->rarity) - 1; tier >= 0 && !next[i]; --tier) {
            const Rarity rarity = static_cast<Rarity>(tier);
            if (takeFromInventory(record, index, rarity, 1)) {
                next[i] = presetItem(preset[i]->petalType, rarity);
            }
        }
        if (next[i]) ++result.downgraded;
        else ++result.missing;
    }

    // A slot that ends up holding what it already held keeps its own item,
    // and with it whatever per-instance state it carried.
    for (std::size_t i = 0; i < next.size() && i < previous.size(); ++i) {
        if (next[i] && previous[i] && previous[i]->petalType == next[i]->petalType &&
            previous[i]->rarity == next[i]->rarity) {
            next[i] = previous[i];
        }
    }
    record.loadout = std::move(next);
    return result;
}

/// One loadout slot as a body in `realm` actually wears it.
///
/// The maze plays the account's ring one rarity DOWN, and an orbiting slot
/// still above mythic after that shift is benched -- left empty on the body
/// and untouched on the account, which is the reference's applyMazeLoadout.
/// The storage row behind the ring shifts too but is never capped: it orbits
/// nothing, so nothing can be over-tier in it.
///
/// Both the body and the profile the client draws its loadout bar from go
/// through here, or the bar would advertise a rarity the flower is not
/// actually swinging.
WornSlot wornSlot(const PlayerRecord& record, std::size_t slot, Realm realm) {
    WornSlot worn;
    if (slot >= record.loadout.size() || !record.loadout[slot].has_value()) return worn;

    const StoredItem& item = *record.loadout[slot];
    worn.petalIndex = content().petalIndex(item.petalType);
    // A stored petal this build no longer has leaves the slot empty rather
    // than resolving to whatever index 0 happens to be.
    if (worn.petalIndex == kInvalidIndex) worn.petalIndex = kNoPetal;
    worn.rarity = item.rarity;

    if (realm == Realm::Maze && worn.petalIndex != kNoPetal) {
        worn.rarity = clampRarity(std::max(0, petalTier(worn.rarity) - 1));
        if (slot < kLoadoutActiveSlots && rarityIndex(worn.rarity) > rarityIndex(Rarity::Mythic)) {
            worn.petalIndex = kNoPetal;
            worn.rarity = Rarity::Common;
        }
    }
    return worn;
}

/// What a brand-new account starts with.
///
/// Keep five Common Basic petals equipped so the first minute is playable
/// (reference behaviour). Ashflor/florrclone also grants twenty Super petals
/// drawn from the ordinary petal pool (not admin/social-only entries).
void grantStarterKit(PlayerRecord& record, const std::string& accountId) {
    const std::uint16_t basic = content().petalIndex("basic");
    if (basic == kInvalidIndex) return;

    constexpr int kStartingEquipped = 5;
    constexpr int kStartingSpares = 5;
    constexpr int kSuperGrant = 20;

    record.loadout.assign(kLoadoutSlots, std::nullopt);
    for (int i = 0; i < kStartingEquipped; ++i) {
        StoredItem item;
        item.type = "petal";
        item.petalType = content().petal(basic).id;
        item.rarity = Rarity::Common;
        record.loadout[static_cast<std::size_t>(i)] = item;
    }
    giveToInventory(record, basic, Rarity::Common, kStartingSpares);

    std::vector<std::uint16_t> pool;
    pool.reserve(content().petalCount());
    for (std::uint16_t i = 0; i < content().petalCount(); ++i) {
        const PetalConfig& petal = content().petal(i);
        if (petal.isAdminPetal) continue;
        if (!petal.tradable) continue;
        if (!petalComesAt(petal, Rarity::Super)) continue;
        pool.push_back(i);
    }
    if (pool.empty()) return;
    // Stable per-account mix without pulling world RNG into account creation.
    std::uint64_t seed = 0xcbf29ce484222325ull;
    for (unsigned char c : accountId) {
        seed ^= c;
        seed *= 0x100000001b3ull;
    }
    for (int n = 0; n < kSuperGrant; ++n) {
        seed = seed * 6364136223846793005ull + 1ull;
        const std::uint16_t pick = pool[static_cast<std::size_t>((seed >> 33) % pool.size())];
        giveToInventory(record, pick, Rarity::Super, 1);
    }
}

/// "Legendary Soldier Ant": the killer's tier and its display name.
///
/// The name, not the config id: the browser client built this from the id, so
/// `baby_ant` read "Common Baby_ant", but ids are for commands and internals
/// and never shown to a player. A player killer reads "Common Player", as it
/// did there. An empty string means
/// the killer is unknown, which is what leaves the client on its own fallback
/// wording.
std::string killerLabel(const World& world, Entity killer) {
    if (killer == NULL_ENTITY || !world.isAlive(killer)) return {};
    if (world.has<PlayerTag>(killer)) return "Common Player";
    const MobType* type = world.tryGet<MobType>(killer);
    if (type == nullptr) return {};
    const std::string& name = content().mob(type->configIndex).name;
    if (name.empty()) return {};
    return std::string(rarityLabel(type->rarity)) + " " + name;
}

/// How many rows of the global feed are kept. The browser build trims to the
/// last thousand on every write, so a feed that has been running for months is
/// still one screen of scrollback rather than a megabyte of the save file.
constexpr std::size_t kNotificationHistory = 1000;

/// A double the way JavaScript would print it: no trailing ".0" on a whole
/// number, which is what a star award always is in practice, and no exponent
/// for anything a code could plausibly be worth.
std::string numberText(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value));
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

/// "@handle [Flower]" as chat credits a player with a deed: the account's
/// handle in green, then the flower's name -- not the account's -- in yellow
/// brackets. One rendering, so a boss kill and an apex pickup name the same
/// player the same way.
std::string creditedPlayerMarkup(const Session& session) {
    const std::string playerName =
        session.displayName.empty() ? session.username : session.displayName;
    return "<span style=\"color: #00ff00;\">@" + session.username +
           "</span> [<span style=\"color: yellow;\">" + playerName + "</span>]";
}

} // namespace

// Outside the file's own namespace, and declared in game_server.h, because the
// console's `notification` command (server/chat_commands.cpp) asks it which
// tags are real.
net::NotificationKind notificationKind(const std::string& type) {
    if (type == "super_craft") return net::NotificationKind::SuperCraft;
    if (type == "unique_craft") return net::NotificationKind::UniqueCraft;
    if (type == "apex_craft") return net::NotificationKind::ApexCraft;
    if (type == "star_code") return net::NotificationKind::StarCode;
    if (type == "universal_craft") return net::NotificationKind::UniversalCraft;
    if (type == "permanent_hyper_forge") return net::NotificationKind::PermanentHyperForge;
    return net::NotificationKind::Generic;
}

struct GameServer::PendingLocalHyper {
    HyperForgePlan plan;
    std::map<std::string, std::string> expectedPlayers;
    std::map<std::string, double> councilActivityCredits;
    std::string expectedForges, user, token, titan;
    Json forges;
    net::ConnectionId connection = 0;
    std::uint16_t input = 0, output = 0;
    double startedAt = 0, publishAt = 0;
    bool submitted = false, publishing = false;
    bool cancelledForConflict = false;
    int retriesRemaining = 1;

    void capturePlan(Database& database, HyperForgePlan next) {
        const auto& current = database.storedTable("hyperForges");
        expectedForges = current.dump(0);
        forges = current;
        forges[next.forgeKey] = next.entry;
        expectedPlayers.clear();
        const auto observedAt = static_cast<double>(database.nowMillis());
        for (auto& [id, row] : next.players) {
            rememberHyperHoldings(row, observedAt);
            expectedPlayers.emplace(id, database.playerSnapshotText(id));
        }
        plan = std::move(next);
    }
};

GameServer::GameServer() {
    net::web::setCouncilHandler([this](const std::string& token,const std::string& body) {
        if (persistenceFrozen_ || (pendingLocalHyper_ && pendingLocalHyper_->publishing))
            return std::string("{\"ok\":false,\"error\":\"authority_unavailable\"}");
        const auto request = Json::parseOrNull(body);
        const auto action = request["action"].asString();
        if (action.rfind("skin-review-", 0) == 0)
            return relaySkinReview(token, request).dump();
        const auto result = council::request(database_, token, request).dump();
        if (action == "identity" || action == "ranking-data") return result;
        if (!commitBeforeReply()) return std::string("{\"ok\":false,\"error\":\"authority_unavailable\"}");
        return result;
    });
}
GameServer::~GameServer() {
    pollLocalHyper(true);
    net::web::setCouncilHandler({});
}

std::string GameServer::adminDbKey() const {
    if (!config_.fixedAdminDbKey.empty()) return config_.fixedAdminDbKey;
    std::string key;
    std::string error;
    return admin_db::readKeyFile(adminDbKeyPath_, key, error) ? key : std::string();
}

bool GameServer::start(const ServerConfig& config, std::string& errorOut) {
    config_ = config;
    sharedSkinSources_ = {};
    nextSharedSkinExpiryCheck_ = 0;
    // The configured bot population, if the caller named one. Same field
    // `/admin set_bot_count` writes, so the two cannot disagree.
    botCountOverride_ = config.botCount;

    if (!loadContent(config.dataDir, errorOut)) return false;
    for (const std::string& warning : content().warnings()) {
        contentWarning("[content] %s\n", warning.c_str());
    }

    // A database that exists but will not parse is fatal at startup. Coming up
    // with an empty one would serve every returning player a blank account and,
    // worse, save that over the file they still had.
    if (config.authorityCommit) {
        if (!database_.loadAuthoritative(config.authoritySnapshot, config.authorityCommit, errorOut)) return false;
    } else if (!database_.load(config.databasePath, errorOut)) return false;
    if(!config_.authorityHyper)foldLookAlikeSkins();

    // Kept in a file beside the database, never anywhere a client can reach:
    // reading it is what proves an admin also has the machine. A first start
    // writes one; after that the file is the operator's, and only they change
    // it. Its key is not printed here -- the log is read by more than the
    // file is.
    if (!config.fixedAdminDbKey.empty()) {
        adminDbKeyPath_.clear();
        std::printf("[admin] database editor key: %s (fixed)\n", config.fixedAdminDbKey.c_str());
    } else {
        adminDbKeyPath_ = admin_db::keyFileFor(config.databasePath);
        std::string key;
        std::string keyError;
        if (!admin_db::keyFileExists(adminDbKeyPath_)) {
            const std::string address = admin_db::privateAddress();
            if (admin_db::createKeyFile(adminDbKeyPath_,
                                        admin_db::deriveKey(database_.serverSecret(), address),
                                        keyError)) {
                std::printf("[admin] wrote a new database editor key to %s (from %s)\n",
                            adminDbKeyPath_.c_str(),
                            address.empty() ? "no network address" : address.c_str());
            } else {
                std::fprintf(stderr, "[admin] %s\n", keyError.c_str());
            }
        }
        if (admin_db::readKeyFile(adminDbKeyPath_, key, keyError)) {
            std::printf("[admin] database editor key: in %s\n", adminDbKeyPath_.c_str());
        } else {
            std::fprintf(stderr, "[admin] database editor locked: %s\n", keyError.c_str());
        }
    }

    rng_.reseed(config.worldSeed);
    // Derived from the same seed, so a world is still reproducible end to end,
    // but a stream of its own -- see botRng_.
    botRng_.reseed(config.worldSeed ^ 0x80757331B07B07ull);
    sacrificeRng_.reseed(config.worldSeed ^ 0x5AC21F1CEull);
    pyramidMapRng_.reseed(config.worldSeed ^ 0x505952414D4944ull);
    pyramidMaps_.clear();
    pyramidCycle_ = pyramidRotationCycle();
    terrain_ = std::make_unique<Terrain>();
    // Every map the data directory stages, in manifest order: each one gets a
    // realm, a tile grid inside `terrain_` and an annotation layer inside
    // `worldMaps_`. A map that will not load IS fatal -- a teleporter aimed at
    // a missing map would drop players into a realm of solid wall -- while an
    // annotation layer that will not load is only reported.
    if (!worldMaps_.load(config.dataDir, terrain_.get(), errorOut)) return false;
    pyramidLayouts_.clear();
    for (const MapData& map : worldMaps_.maps()) {
        if (map.biome() == "pyramid" && !map.prestige()) pyramidLayouts_.push_back(map.realm());
    }
    for (const std::string& warning : worldMaps_.warnings()) {
        contentWarning("[map] %s\n", warning.c_str());
    }
    // The broadphase was built before the maps were: its layers are sized to
    // the default world until it is told what shape each realm really is.
    grid_.sizeToRealms(*terrain_);
    mobTargetGrid_.sizeToRealms(*terrain_);
    playerTargetGrid_.sizeToRealms(*terrain_);
    pickupGrid_.sizeToRealms(*terrain_);
    snapshotGrid_.sizeToRealms(*terrain_);

    movement_ = std::make_unique<MovementSystem>();
    // The AI caches queries against one world and wanders from its own
    // stream, so it takes both at construction rather than per call.
    mobAi_ = std::make_unique<MobAiSystem>(world_, config.worldSeed ^ 0x9E3779B9ull);
    petals_ = std::make_unique<PetalSystem>();
    combat_ = std::make_unique<CombatSystem>();
    combat_->onPracticeHit = [this](Entity player, Entity dummy, double amount, double nowMillis) {
        const auto* account = world_.tryGet<PlayerAccount>(player);
        auto* session = account ? sessionFor(account->connection) : nullptr;
        if (!session || session->practiceTrial.player != player) return;
        auto& trial = session->practiceTrial;
        if (trial.dummy == dummy && nowMillis < trial.endsAt) trial.damage += amount;
    };
    combat_->sizeContactGrid(*terrain_);
    spawning_ = std::make_unique<SpawnSystem>();
    spawning_->sizePlacementGrids(*terrain_);
    if (config.loadShedding) {
        petals_->volleyEntityLimit = 96;
        std::printf("[capacity] load shedding enabled; 20 TPS, no player cap\n");
    }
    spawning_->seedBossClocks(config.worldSeed ^ 0xB055C10C5ull);
    modes_ = std::make_unique<ModeSpawner>();
    dungeons_ = std::make_unique<DungeonSystem>();
    loot_ = std::make_unique<LootSystem>();
    if (!loot_->loadTables(content(), config.dataDir + "/mob_drops.json", errorOut)) return false;
    // The NPCs the maps ask for, resolved against the content once. A site
    // that names nothing placeable is reported rather than fatal: the map
    // still plays, it is only missing whoever was meant to stand there.
    npcs_ = std::make_unique<NpcSystem>();
    npcs_->seed(config.worldSeed ^ 0x0DAC1E5EEDull);
    {
        std::vector<std::string> npcWarnings;
        npcs_->loadSites(worldMaps_, content(), npcWarnings);
        for (const std::string& warning : npcWarnings) {
            contentWarning("[map] %s\n", warning.c_str());
        }
    }

    // Wire ids are unique across the whole server, so the id space belongs
    // here rather than to any one system. A system left unwired still
    // simulates -- its entities are simply not replicated, which is what a
    // headless test wants and what this must not be in production.
    petals_->allocateNetId = [this] { return netIds_.next(); };
    mobAi_->allocateNetId = [this] { return netIds_.next(); };
    spawning_->netIds = &netIds_;
    loot_->netIds = &netIds_;
    npcs_->netIds = &netIds_;
    // One table, two readers: what a corpse pays XP for and what it reserves
    // its drops for must be the same answer. rebuildSquadIndex() refreshes it
    // once a tick; the pointer never moves.
    loot_->squads = &squadIndex_;
    combat_->squads = &squadIndex_;

    // The annotation layer is a read-only service, so the systems that need it
    // hold a pointer rather than being handed it per call. Left null they fall
    // back to behaviour with no map at all, which is what every unit test gets.
    // In production that would silently cost the spawn rectangles, the biome
    // tiers and every teleporter on the map, so it is wired here, once.
    spawning_->worldMaps = &worldMaps_;
    updatePyramidSpawnRealms();
    movement_->worldMaps = &worldMaps_;
    // A pad leads to another map, which is a realm change: a new tile grid on
    // the wire and a cleared view on the client. Only the connection layer can
    // do that, so the movement pass reports the jump and this carries it out.
    movement_->onTeleport = [this](Entity entity, Realm realm, Vec2 position) {
        // A dungeon copy's own pad leads back out beside the nest it was
        // entered through, wherever the pad's targetMap points: eight copies
        // of one map share one pad, and each belongs to a different nest.
        if (const Transform* at = world_.tryGet<Transform>(entity)) {
            const Body* body = world_.tryGet<Body>(entity);
            dungeons_->exitFor(at->realm, rng_, body != nullptr ? body->radius : 25.0, realm,
                               position);
        }
        moveEntityToRealm(entity, realm, position);
    };
    dungeons_->worldMaps = &worldMaps_;
    dungeons_->moveToRealm = [this](Entity entity, Realm realm, Vec2 position) {
        moveEntityToRealm(entity, realm, position);
    };
    dungeons_->admits = [this](Entity entity) { return botForEntity(entity) == nullptr; };
    dungeons_->waveSummary = [this](Entity entity, const std::string& text) {
        if (const Session* session = sessionForEntity(entity)) {
            if (auto* connection = listener_.find(session->connection))
                sendNotice(*connection, net::NoticeSeverity::WaveResult, text);
        }
    };
    dungeons_->waveAward = [this](Entity entity, std::uint64_t cleared, bool bonus) -> DungeonSystem::WaveAward {
        auto* session = sessionForEntity(entity);
        if (!session || !session->authenticated() || session->entity != entity) return {};
        auto* live = world_.tryGet<PlayerProgress>(entity);
        bool reachedDailyCap = false;
        const int stars = database_.awardWaveStars(session->userId, &reachedDailyCap, cleared);
        const bool hammer = bonus && database_.awardWaveBonusHammer(session->userId);
        if (stars <= 0 && !hammer) return {0, reachedDailyCap};
        if (live && stars > 0) {
            live->stars += stars;
            database_.progress(session->userId).stars = live->stars;
        }
        session->profileDue = true;
        if (auto* connection = listener_.find(session->connection)) {
            std::string message = "Wave 第 " + std::to_string(cleared) + " 波通关";
            if (stars > 0) message += " · +" + std::to_string(stars) + " 星星";
            if (hammer) message += " · +1 永恒限时锤（1 小时）";
            sendNotice(*connection, net::NoticeSeverity::Good, message);
        }
        return {stars, reachedDailyCap, hammer};
    };
    dungeons_->waveRevived = [this](Entity entity) {
        auto& health = world_.get<Health>(entity);
        health.current = health.max;
        health.invulnerableUntilMillis = clockMillis_ + kRespawnInvulnerabilitySeconds * 1000.0;
        if (auto* mana = world_.tryGet<ManaPool>(entity)) mana->current = 0.0;
        world_.remove<Dead>(entity);
        if (world_.has<Unrevivable>(entity)) world_.remove<Unrevivable>(entity);
        onPlayerRevived(entity, NULL_ENTITY);
    };
    // Bots exist to populate the OVERWORLD, and their controller is written
    // against it: its flow field, its ray casts and its broadphase queries
    // all read that one map. A bot carried through a pad would steer around
    // walls it is not standing among, so pads simply do not take bots.
    movement_->takesTeleporters = [this](Entity entity) { return botForEntity(entity) == nullptr; };
    loot_->terrain = terrain_.get();
    loot_->immutableWorldTerrain = true;
    loot_->bonusMultiplier = [this](Entity player) {
        const auto* session=sessionForEntity(player);
        return session && session->authenticated() ? activeDropBonus(database_.progress(session->userId),dropBonusNow()) : 1;
    };
    combat_->terrain = terrain_.get();

    // A revived flower has to be un-announced to its own client, and only the
    // connection layer can do that.
    petals_->onPlayerRevived = [this](Entity revived, Entity reviver) {
        onPlayerRevived(revived, reviver);
    };
    petals_->canMarkPlayer = [this](Entity target, Entity marker, Rarity rarity) {
        return canMarkPlayer(target, marker, rarity);
    };
    petals_->onPlayerMarked = [this](Entity target, Entity marker, Rarity rarity) {
        return sendMarkedPlayerToHel(target, marker, rarity);
    };

    // The maze is a daily-seeded region of the world with its own walls. Its
    // geometry is a pure function of the day number, so the server picks the
    // day once at boot; every collision query inside the region then answers
    // against the same maze for the whole session.
    setActiveMazeDay(currentMazeDay());

    listener_.certPath = config.certPath;
    listener_.keyPath = config.keyPath;
    listener_.webRoot = config.webRoot;
    if (!listener_.start(config.port, errorOut)) return false;

    // Seeded before the first tick can set it: a message may be serviced ahead
    // of the first tick, and a deadline stamped from a zero clock is one that
    // has already passed.
    clockMillis_ = monotonicMillis();
    // The maps' NPCs are part of the maps, so they stand from boot rather than
    // from whenever the first player happens to arrive.
    npcs_->placeMissing(world_, *terrain_, content(), clockMillis_);
    if(config_.authorityDeltaCommit) {
        if(database_.dirty()&&!database_.save()){errorOut="initial authority save failed";return false;}
        if(!database_.setDeltaSink(config_.authorityDeltaCommit)){errorOut="authority delta adapter initialization failed";return false;}
    }
    if (config_.backgroundSave && !database_.enableBackgroundSave(errorOut, config_.tickProbe)) return false;
    running_ = true;
    return true;
}

void GameServer::run() {
    nextTickMillis_ = monotonicMillis();
    wallClockMillis_ = clockMillis_ = nextTickMillis_;
    while (step()) {
    }
    shutdown();
}

void GameServer::shutdown() {
    if(shutdownDone_)return;
    shutdownDone_=true;
    pollLocalHyper(true);
    // Flush account progress before exiting; an orderly shutdown must not cost
    // anyone their session.
    if (database_.backgroundSaveEnabled()) {
        captureLiveProgress();
        if (persistenceFrozen_ || !saveDatabase(false, true)) {
            std::fprintf(stderr, "[persist-shutdown] final save failed\n");
            exitCode_ = 1;
        }
    } else if (!persistenceFrozen_ && (!config_.authorityDeltaCommit || !authorityStopPrepared_)) {
        persistAll();
    }
    if(database_.authoritative()) {
        const bool saved=!persistenceFrozen_&&!database_.commitUncertain()&&!database_.commitPending()&&
            commitBeforeReply()&&!database_.dirty();
        const bool released=saved&&(config_.authorityReleaseAsync?authorityStopReleased_:
            (!config_.authorityReleaseUnused||config_.authorityReleaseUnused({})));
        std::printf("[authority_shutdown] {\"saved\":%s,\"released\":%s}\n",saved?"true":"false",released?"true":"false");
        if(!saved||!released){persistenceFrozen_=true;exitCode_=1;}
    }
    listener_.stop();
}

bool GameServer::refreshAuthority() {
    if(!config_.authorityRefresh)return true;
    if(!commitBeforeReply())return false;
    std::string error;
    const auto snapshot=config_.authorityRefresh();
    if(snapshot.empty()||!database_.refreshAuthoritative(snapshot,error)) {
        persistenceFrozen_=true;running_.store(false);return false;
    }
    for(auto& entry:sessions_)if(entry.second.authenticated())applyAccountToSession(entry.second);
    return true;
}

bool GameServer::acquireAuthority(const std::string& id,const std::string& tokenHash) {
    if(!config_.authorityAcquire)return true;
    const auto result=config_.authorityAcquire(id,tokenHash);
    if(!result["ok"].asBool())return false;
    if(std::find(authorityLiveAccounts_.begin(),authorityLiveAccounts_.end(),id)==authorityLiveAccounts_.end())
        authorityLiveAccounts_.push_back(id);
    return database_.replaceAuthoritativeProgress(id,result["record"]);
}

void GameServer::releaseUnusedAuthority() {
    if((!config_.authorityReleaseUnused&&!config_.authorityReleaseAsync)||persistenceFrozen_)return;
    std::vector<std::string> active;
    for(const auto& entry:sessions_)if(entry.second.authenticated())active.push_back(entry.second.userId);
    std::sort(active.begin(),active.end());active.erase(std::unique(active.begin(),active.end()),active.end());
    if(active==authorityLiveAccounts_&&!authorityReleasePending_&&
        (!config_.authorityReleaseAsync||monotonicMillis()<authorityNextReleaseCheck_))return;
    // Releasing ownership must cover mutations made while an older save was
    // pending, including final progress captured by a disconnect.
    if(config_.authorityReleaseAsync) {
        captureLiveProgress();
        authorityReleaseTicket_=database_.mutationGeneration();
    }
    const bool committed=config_.authorityReleaseAsync?
        drainAuthoritative(true,authorityReleaseTicket_)==net::Connection::DrainDecision::Allow:commitBeforeReply();
    if(!committed) {
        if(database_.commitPending())return;
        persistenceFrozen_=true;running_.store(false);return;
    }
    bool released=false;
    if(config_.authorityReleaseAsync) {
        const auto result=config_.authorityReleaseAsync(active);
        authorityReleasePending_=result["pending"].asBool();if(authorityReleasePending_)return;
        released=result["ok"].asBool();
    } else released=config_.authorityReleaseUnused(active);
    if(!released) {
        persistenceFrozen_=true;running_.store(false);return;
    }
    authorityLiveAccounts_=std::move(active);
    authorityReleaseTicket_=UINT64_MAX;authorityNextReleaseCheck_=monotonicMillis()+1000;
}

bool GameServer::commitBeforeReply() {
    if (!database_.authoritative()) return true;
    if (persistenceFrozen_) return false;
    captureLiveProgress();
    if (database_.commitUncertain() || (database_.dirty() && !database_.save())) {
        if(database_.commitPending()&&!database_.commitUncertain())return false;
        persistenceFrozen_ = true;
        running_.store(false);
        return false;
    }
    return !database_.dirty();
}

void GameServer::captureLiveProgress() {
    for (const auto& entry : sessions_) {
        const auto& session = entry.second;
        if (!session.playing() || !world_.isAlive(session.entity)) continue;
        const auto* progress = world_.tryGet<PlayerProgress>(session.entity);
        const auto* record = database_.findProgress(session.userId);
        if (progress && (!record || progress->stars != record->stars ||
            progress->totalXp != (session.realm == Realm::Maze ? record->mazeTotalXp : record->totalXp)))
            persistPlayer(session);
    }
}

net::Connection::DrainDecision GameServer::drainAuthoritative(bool durable,std::uint64_t& ticket) {
    using Decision=net::Connection::DrainDecision;
    if(persistenceFrozen_||database_.commitUncertain()){persistenceFrozen_=true;running_.store(false);return Decision::Fail;}
    if(!durable)return Decision::Allow;
    if(ticket==UINT64_MAX){captureLiveProgress();ticket=database_.mutationGeneration();}
    for(int pass=0;pass<2;++pass) {
        if(database_.committedGeneration()>=ticket)return Decision::Allow;
        if(!database_.save()) {
            if(database_.commitPending()&&!database_.commitUncertain())return Decision::Defer;
            persistenceFrozen_=true;running_.store(false);return Decision::Fail;
        }
    }
    return database_.committedGeneration()>=ticket?Decision::Allow:Decision::Defer;
}

std::size_t GameServer::persistAll() {
    if (persistenceFrozen_) return 0;
    std::size_t saved = 0;
    for (const auto& entry : sessions_) {
        if (!entry.second.playing()) continue;
        persistPlayer(entry.second);
        ++saved;
    }
    saveDatabase();
    return saved;
}

void GameServer::captureBossClocksForSave() {
    // Which bosses stand NOW, not as of the last population pass: the file is
    // what a restart -- a crash included -- comes back to, so a boss placed or
    // killed in the last half second has to be on the right side of it.
    if (bossClocksRestored_) {
        spawning_->noteStandingBosses(world_, commands_);
        persistBossClocks(true);
    }
}

bool GameServer::saveDatabase(bool periodic, bool stopping) {
    captureBossClocksForSave();
    if (periodic && database_.backgroundSaveEnabled()) {
        database_.requestBackgroundSave();
        return true;
    }
    if (stopping) return database_.finishBackgroundSave();
    return database_.save();
}

void GameServer::restoreBossClocks(double nowMillis) {
    bossClocksRestored_ = true;
    const Json& table = database_.storedTable("bossClocks");
    if (!table.isObject()) return;
    // Stored on the wall clock, which is the only one two runs share; this
    // run's clock is whatever tick() is handed. A ready time further out than
    // a whole cooldown can only be a wall clock that has since been set back,
    // and is pulled in to one cooldown rather than left to wait it out.
    const double offset = nowMillis - static_cast<double>(database_.nowMillis());
    for (const std::string& biome : table.keys()) {
        const Json& entry = table[biome];
        if (!entry["uniqueReadyAt"].isNumber() || !entry["apexReadyAt"].isNumber()) continue;
        const double unique = std::min(entry["uniqueReadyAt"].asDouble() + offset,
                                       nowMillis + kUniqueSpawnCooldownMillis);
        const double apex = std::min(entry["apexReadyAt"].asDouble() + offset,
                                     nowMillis + kApexSpawnCooldownMillis);
        spawning_->restoreBossClock(biome, unique, apex);
        savedBossClocks_.push_back(SavedBossClock{biome, unique, apex});
    }
}

void GameServer::persistBossClocks(bool refreshWallDeadline) {
    const double offset = static_cast<double>(database_.nowMillis()) - clockMillis_;
    const double worldLag = wallClockMillis_ - clockMillis_;
    for (const SpawnSystem::BiomeBossClock& clock : spawning_->bossClocks()) {
        if (!clock.scattered) continue;
        // A boss still standing is one a restart would take away unkilled, so
        // the cooldown it started is handed back: its clock is saved as of the
        // moment the boss spent it, which the next run reads as ready. Once it
        // dies the real ready time goes back in.
        const double unique = clock.uniqueStanding && !clock.uniqueDeathCooldown
                                  ? clock.uniqueReadyMillis - kUniqueSpawnCooldownMillis
                                  : clock.uniqueReadyMillis;
        const double apex = clock.apexStanding ? clock.apexReadyMillis - kApexSpawnCooldownMillis
                                               : clock.apexReadyMillis;
        SavedBossClock* saved = nullptr;
        for (SavedBossClock& candidate : savedBossClocks_) {
            if (candidate.biome == clock.biome) saved = &candidate;
        }
        if (saved != nullptr && saved->uniqueReadyMillis == unique &&
            saved->apexReadyMillis == apex &&
            (!refreshWallDeadline || std::abs(saved->worldLagMillis - worldLag) < 1.0)) {
            continue;
        }
        if (saved == nullptr) {
            savedBossClocks_.push_back(SavedBossClock{clock.biome});
            saved = &savedBossClocks_.back();
        }
        saved->uniqueReadyMillis = unique;
        saved->apexReadyMillis = apex;
        saved->worldLagMillis = worldLag;
        Json entry = Json::object();
        entry["uniqueReadyAt"] = std::round(unique + offset);
        entry["apexReadyAt"] = std::round(apex + offset);
        database_.rawTable("bossClocks")[clock.biome] = std::move(entry);
        database_.markDirty();
    }
}

void GameServer::queueSharedView(const Session& session,net::Connection& connection,Json query) {
    if(sharedViews_.size()>=256||sharedViews_.count(connection.id()))return;
    sharedViews_[connection.id()]={std::move(query),crypto::secureRandom().hex(16),session.userId,monotonicMillis()+5000};
}
void GameServer::serviceSharedViews() {
    if(!config_.authorityHyper)return;
    for(auto it=sharedViews_.begin();it!=sharedViews_.end();) {
        auto& work=it->second;auto* session=sessionFor(it->first);auto* peer=listener_.find(it->first);
        const bool valid=session&&peer&&session->userId==work.user;
        Json input=Json::object();input["action"]="publicView";input["attempt"]=work.attempt;input["query"]=work.query;
        const auto result=valid?config_.authorityAuth(input):Json();
        if(valid&&result["pending"].asBool()&&monotonicMillis()<work.deadline){++it;continue;}
        if(valid&&result["ok"].asBool()) {
            const auto& view=result["body"];ByteWriter w;
            if (work.query["kind"].asString() == "practice") {
                auto board = view;
                // The current shard owns this player's recent writes. The
                // authority may not have checkpointed them yet.
                if (const auto* record = database_.findProgress(work.user)) {
                    board["own"] = record->extra["practiceRecords"]["rows"];
                    auto& rows = board["own"].items();
                    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                        return a["damage"].asDouble() > b["damage"].asDouble();
                    });
                }
                writePracticeBoard(w, board);
            } else if(work.query["kind"].asString()=="titan") {
                w.u8(static_cast<std::uint8_t>(net::ServerMessage::TitanHolder));w.u16(work.query["petal"].asInt());
                w.str(view["name"].asString());w.u32(static_cast<std::uint32_t>(view["remaining"].asDouble()));
                w.u8(view["cost"].asInt());w.u8(std::min(255,view["held"].asInt()));
            } else {
                w.u8(static_cast<std::uint8_t>(net::ServerMessage::Leaderboard));w.u8(view["rows"].size());
                w.u32(view["users"].asInt());w.u32(view["active"].asInt());
                for(const auto& row:view["rows"].items()){w.str(row["name"].asString());w.u16(row["level"].asInt());w.f64(row["xp"].asDouble());}
                w.u32(view["total"].asInt());w.u32(view["offset"].asInt());w.u16(view["holdings"].size());
                for(const auto& row:view["holdings"].items()){w.str(row["name"].asString());w.str(row["petal"].asString());w.u32(row["count"].asInt());w.u32(row["ownerTotal"].asInt());}
                w.u8(view["playtime"].size());
                for (const auto& row : view["playtime"].items()) { w.str(row["name"].asString()); w.f64(row["seconds"].asDouble()); }
                w.u32(view["levelRank"].asInt());
                w.u32(view["playtimeRank"].asInt());
                w.u32(view["hyperRank"].asInt());
            }
            peer->send(w);
        }
        input["action"]="finishView";config_.authorityAuth(input);it=sharedViews_.erase(it);
    }
}

bool GameServer::beginSharedHyper(Session& session,net::Connection& connection,Json request,std::uint16_t petal,bool forge) {
    if(!config_.authorityAuth||!pendingHyper_.attempt.empty())return false;
    request["account"]=session.userId;
    pendingHyper_={request,crypto::secureRandom().hex(16),"",session.userId,connection.id(),petal,forge};
    return true;
}

void GameServer::finishSharedHyper(const Json& result) {
    if(pendingHyper_.attempt.empty())return;
    const bool ok=result["ok"].asBool();
    const auto reason=result["reason"].asString(ok?"超凡交接已完成。":"超凡事务未完成，未确认扣料；请稍后查询或重试。");
    auto* peer=listener_.find(pendingHyper_.connection);auto* session=sessionFor(pendingHyper_.connection);
    if(peer&&session&&session->userId==pendingHyper_.user) {
        if(pendingHyper_.forge) {
            ByteWriter w;w.u8(static_cast<std::uint8_t>(net::ServerMessage::TitanForgeResult));w.boolean(ok);
            const auto output=content().petalIndex(hyperForgeOutput(content().petal(pendingHyper_.petal).id));
            w.u16(ok?output:pendingHyper_.petal);w.str(ok?"":reason);w.u32(ok ? 1 : 0);peer->send(w);
            if(ok)announceRareCraft(*session,output,Rarity::Apex,1,result["consumed"].asInt(),true);
        } else sendNotice(*peer,ok?net::NoticeSeverity::Info:net::NoticeSeverity::Warning,reason);
    }
    pendingHyper_=PendingHyper{};
}

bool GameServer::serviceHyperBarrier() {
    if(!config_.authorityHyper)return false;
    const auto invoke=[&](const char* action,const std::string& id=std::string()) {
        Json input=Json::object();input["action"]=action;input["transaction"]=id;return config_.authorityAuth(input);
    };
    if(!pendingHyper_.attempt.empty()&&pendingHyper_.transaction.empty()) {
        Json input=Json::object();input["action"]="hyperBegin";input["attempt"]=pendingHyper_.attempt;input["request"]=pendingHyper_.request;
        const auto begun=config_.authorityAuth(input);
        if(!begun["pending"].asBool()) {
            if(begun["ok"].asBool())pendingHyper_.transaction=begun["transaction"]["id"].asString();
            else finishSharedHyper(begun);
        }
    }
    const auto status=invoke("hyperService");
    if(!status["ok"].asBool()){persistenceFrozen_=true;running_.store(false);return true;}
    hyperBarrierFrozen_=status["freeze"].asBool();
    if(!hyperBarrierFrozen_)return false;
    const auto& tx=status["transaction"];const auto id=tx["id"].asString();
    // Finish already admitted authentication before taking the final snapshot;
    // do not admit new messages or advance the world while the barrier exists.
    if(!authorityAuthWork_.empty())return true;
    if(hyperBarrierCaptured_!=id){persistAll();hyperBarrierCaptured_=id;}
    if(database_.dirty()||database_.commitPending()) {
        if(!database_.save()&&!database_.commitPending()){persistenceFrozen_=true;running_.store(false);}
        return true;
    }
    if(tx["stage"].asString()=="freezing") {
        if(hyperBarrierReady_!=id) {
            const auto result=invoke("hyperReady",id);
            if(!result["pending"].asBool()) {
                if(!result["ok"].asBool()){persistenceFrozen_=true;running_.store(false);}
                else hyperBarrierReady_=id;
            }
        }
        return true;
    }
    if(tx["stage"].asString()=="settled") {
        if(hyperBarrierApplied_!=id) {
            if(!database_.applyAuthoritativeDelta(status["delta"])) {persistenceFrozen_=true;running_.store(false);return true;}
            for(auto& [connectionId,session]:sessions_)if(status["delta"]["players"].contains(session.userId)) {
                if(session.playing())applyAccountToSession(session);
                if(auto* peer=listener_.find(connectionId)) {
                    sendProfile(session,*peer);
                    if(tx["result"]["ok"].asBool()&&session.userId!=tx["input"]["account"].asString())
                        sendNotice(*peer,net::NoticeSeverity::Warning,"你的超凡持有权已转移，材料已按原规则返还。");
                }
            }
            hyperBarrierApplied_=id;
            if(pendingHyper_.transaction==id)finishSharedHyper(tx["result"]);
        }
        const auto result=invoke("hyperThaw",id);
        if(!result["pending"].asBool()&&!result["ok"].asBool()){persistenceFrozen_=true;running_.store(false);}
    }
    return true;
}

bool GameServer::step(double timerLagMillis) {
    if(!running_.load()&&config_.authorityAuth&&(hyperBarrierFrozen_||!pendingHyper_.attempt.empty())) {
        if(serviceHyperBarrier())return !persistenceFrozen_;
    }
    if (!running_.load()) {
        if(config_.authorityDeltaCommit&&!persistenceFrozen_) {
            if(!authorityStopPrepared_){authorityStopPrepared_=true;persistAll();}
            captureLiveProgress();
            if(database_.dirty()||database_.commitPending()) {
                if(!database_.save()&&(database_.commitUncertain()||!database_.commitPending())){persistenceFrozen_=true;return false;}
                if(database_.dirty()||database_.commitPending())return true;
            }
            if(config_.authorityAuth) {
                Json input=Json::object();input["action"]="quiesce";
                const auto result=config_.authorityAuth(input);if(result["pending"].asBool())return true;
                if(!result["ok"].asBool()){persistenceFrozen_=true;return false;}
            }
            if(config_.authorityReleaseAsync&&!authorityStopReleased_) {
                const auto result=config_.authorityReleaseAsync({});if(result["pending"].asBool())return true;
                if(!result["ok"].asBool()){persistenceFrozen_=true;return false;}
                authorityStopReleased_=true;
            }
        }
        return false;
    }
    if (database_.commitUncertain() &&
        (database_.backgroundSaveEnabled() || !commitBeforeReply())) return false;

    const double now = monotonicMillis();
    wallClockMillis_ = now;

    // Sleep in the network poll rather than in a bare sleep, so a packet
    // arriving mid-frame is picked up immediately instead of waiting out
    // the remainder of the tick.
    const int waitMillis = static_cast<int>(std::max(0.0, nextTickMillis_ - now));
    serviceNetwork(std::min(waitMillis, 5));
    if (persistenceFrozen_) return false;
    if (pendingLocalHyper_ && pendingLocalHyper_->publishing) return true;
    if(hyperBarrierFrozen_){nextTickMillis_=monotonicMillis();lastTickWorldMillis_=0;lastDropBonusTickWallMillis_=0;return true;}
    // What the handlers just answered -- a Pong, an AuthResult, a CraftResult
    // -- leaves now, not at the next drain. Under Node that drain is the next
    // step, a whole 60 Hz timer away, or the end of a tick: on loopback it was
    // half of every round trip (median 21 ms without this flush, 10 with it).
    listener_.flush();

    const double tickNow = monotonicMillis();
    if (tickNow >= nextTickMillis_) {
        const double tickLagMillis = tickNow - nextTickMillis_;
        const double worldNow = simulationPacing_.advance(tickNow);
        double raw = lastTickWorldMillis_ > 0
                         ? (worldNow - lastTickWorldMillis_) / 1000.0
                         : net::kTickSeconds;
        lastTickWorldMillis_ = worldNow;
        if (simulationPacing_.ticksPerSecond() < net::kTicksPerSecond &&
            lastDropBonusTickWallMillis_ > 0) {
            const double elapsed = tickNow - lastDropBonusTickWallMillis_;
            const auto now = dropBonusNow();
            bool changed = false;
            for (auto& [id, session] : sessions_) {
                if (session.authenticated() && session.playing()) {
                    changed |= compensateSlowDropBonus(database_.progress(session.userId),
                                                       now, elapsed, raw * 1000.0);
                }
            }
            if (changed) database_.markDirty();
        }
        lastDropBonusTickWallMillis_ = tickNow;
        if (raw > kMaxDeltaSeconds) raw = kMaxDeltaSeconds;
        if (simulationPacing_.ticksPerSecond() < net::kTicksPerSecond) {
            smoothedDeltaSeconds_ = net::kTickSeconds;
        } else {
            smoothedDeltaSeconds_ += (raw - smoothedDeltaSeconds_) * kDeltaSmoothing;
        }

        const double tickStarted = monotonicMillis();
        const double probeCpuStart = config_.tickProbe ? tickProbeThreadCpuMillis() : -1.0;
        const double probeWallStart = config_.tickProbe ? std::chrono::duration<double, std::milli>(
            std::chrono::system_clock::now().time_since_epoch()).count() : 0.0;
        tick(tickNow, worldNow);
        const double tookMillis = monotonicMillis() - tickStarted;
        const double probeCpu = tickProbeCpuElapsed(probeCpuStart);
        debugTickAccumMillis_ += tookMillis;
        if (tookMillis > debugTickMaxMillis_) debugTickMaxMillis_ = tookMillis;
        ++debugTickSamples_;
        if (tookMillis >= kSlowTickLogMillis && tookMillis > slowTickWorstMillis_) {
            slowTickWorstMillis_ = tookMillis;
            slowTickLine_ = describeTickPhases(tookMillis);
        }

        const int previousRate = simulationPacing_.ticksPerSecond();
        const bool paceEnabled = config_.loadShedding && config_.adaptivePacing;
        simulationPacing_.observe(tickNow, tookMillis, paceEnabled, paceEnabled && playerCount() > 0);
        const int rate = simulationPacing_.ticksPerSecond();
        if (rate != previousRate) {
            std::printf("[tick] slow protection %d -> %d TPS, last work %.1f ms\n", previousRate, rate, tookMillis);
            nextTickMillis_ = tickNow;
        }
        nextTickMillis_ += simulationPacing_.intervalMillis();
        // A timer never queues up the fires it missed: a tick that overran
        // simply makes the next one land immediately, it does not run twice
        // to catch up. Replaying would advance the fixed-step mob half once
        // per replay while the dt-scaled half, which has just had its delta
        // reset to almost nothing, stood still.
        const double earliest = monotonicMillis() + (rate < net::kTicksPerSecond ? 4.0 : 0.0);
        if (nextTickMillis_ < earliest) nextTickMillis_ = earliest;

        if (config_.tickProbe)
            recordTickProbe(tickNow, timerLagMillis, tickNow - now, tickLagMillis,
                            tookMillis, probeCpu, probeWallStart, previousRate, rate);
        recordTickCadence(tickNow, tickLagMillis, tookMillis);
        if (tickNow >= nextDebugStatsMillis_ || rate != previousRate) {
            broadcastDebugStats();
            nextDebugStatsMillis_ = tickNow + 1000.0;
            if (!slowTickLine_.empty()) {
                std::printf("%s\n", slowTickLine_.c_str());
                std::fflush(stdout);
                slowTickLine_.clear();
            }
            slowTickWorstMillis_ = 0;
        }
    }

    return running_.load();
}

void GameServer::serviceNetwork(int timeoutMillis) {
    if (persistenceFrozen_) return;
    pollLocalHyper();
    if (persistenceFrozen_ || (pendingLocalHyper_ && pendingLocalHyper_->publishing)) return;
    if (!pendingLocalHyper_) pollPasswordChange();
    if (persistenceFrozen_) return;
    if(database_.commitPending())database_.save();
    std::vector<net::ConnectionId> authWork;
    for(const auto& entry:authorityAuthWork_){authWork.push_back(entry.first);if(authWork.size()>=4)break;}
    for(const auto id:authWork) {
        auto found=authorityAuthWork_.find(id);if(found==authorityAuthWork_.end())continue;
        if(auto* connection=listener_.find(id)) {
            processingAuthorityAuth_=id;ByteReader message(found->second.packet);onMessage(*connection,message);processingAuthorityAuth_=0;
        } else found->second.done=true;
        found=authorityAuthWork_.find(id);
        if(found!=authorityAuthWork_.end()&&found->second.done) {
            Json finish=Json::object();finish["action"]="finishAuth";finish["attempt"]=static_cast<double>(id);
            config_.authorityAuth(finish);authorityAuthWork_.erase(found);
        }
    }
    std::vector<net::ConnectionId> retry;
    for(const auto& entry:authorityAuthQueue_){retry.push_back(entry.first);if(retry.size()>=4)break;}
    for(const auto id:retry) {
        auto bytes=std::move(authorityAuthQueue_.at(id));authorityAuthQueue_.erase(id);
        if(auto* connection=listener_.find(id)){ByteReader message(bytes);onMessage(*connection,message);}
    }
    serviceSharedViews();
    if(serviceHyperBarrier())return;
    listener_.poll(*this, timeoutMillis);
    releaseUnusedAuthority();
}

void GameServer::serviceArrivals() {
    if (!running_.load()) return;
    wallClockMillis_ = monotonicMillis();
    if (!config_.tickProbe) {
        serviceNetwork(0);
        listener_.flush();
        return;
    }

    ArrivalProbe probe;
    arrivalProbe_ = &probe;
    const double start = monotonicMillis();
    const double cpuStart = tickProbeThreadCpuMillis();
    const auto editsBefore = database_.snapshotEdits();
    serviceNetwork(0);
    const double networkEnd = monotonicMillis();
    listener_.flush();
    arrivalProbe_ = nullptr;
    const double end = monotonicMillis();
    if (end - start < 50.0) return;

    const auto wallEnd = std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Json report = Json::object();
    report["wallEndMs"] = wallEnd;
    report["workMs"] = end - start;
    report["threadCpuMs"] = tickProbeCpuElapsed(cpuStart);
    report["networkMs"] = networkEnd - start;
    report["flushMs"] = end - networkEnd;
    report["messages"] = static_cast<double>(probe.messages);
    report["messageMs"] = probe.messageMillis;
    report["slowestMessage"] = static_cast<double>(probe.slowestMessage);
    report["slowestMs"] = probe.slowestMillis;
    const auto editsAfter = database_.snapshotEdits();
    report["snapshotRowCopies"] = static_cast<double>(editsAfter.copies - editsBefore.copies);
    report["snapshotRowMs"] = editsAfter.wallMs - editsBefore.wallMs;
    Json types = Json::object();
    for (std::size_t id = 0; id < probe.byType.size(); ++id) {
        const auto& message = probe.byType[id];
        if (!message.count) continue;
        Json stats = Json::object();
        stats["count"] = static_cast<double>(message.count);
        stats["wallMs"] = message.millis;
        types[std::to_string(id)] = std::move(stats);
    }
    report["messageTypes"] = std::move(types);
    std::printf("[tick-probe-arrival] %s\n", report.dump().c_str());
}

double GameServer::nextStepDelayMillis() const {
    // The Node timer may poll for maintenance between simulation ticks.
    return std::clamp(nextTickMillis_ - monotonicMillis(), 0.0, 1000.0 / 60.0);
}

std::size_t GameServer::playerCount() const {
    std::size_t n = 0;
    for (const auto& entry : sessions_) {
        if (entry.second.playing()) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void GameServer::recordTickCadence(double nowMillis, double lagMillis, double workMillis) {
    if (tickCadenceStartMillis_ == 0) {
        tickCadenceStartMillis_ = nowMillis;
        return;
    }

    ++tickCadenceSamples_;
    if (workMillis >= net::kTickMillis) ++tickCadenceOverBudget_;
    tickCadenceMaxWorkMillis_ = std::max(tickCadenceMaxWorkMillis_, workMillis);
    tickCadenceMaxLagMillis_ = std::max(tickCadenceMaxLagMillis_, lagMillis);
    const double elapsedMillis = nowMillis - tickCadenceStartMillis_;
    if (elapsedMillis < kTickCadenceLogMillis) return;
    lastMeasuredTps_ = tickCadenceSamples_ * 1000.0 / elapsedMillis;

    const auto players = playerCount();
    if (players > 0 || tickCadenceOverBudget_ > 0) {
        std::printf("[tick] cadence %.2f/%d TPS over %.1f s, players=%zu, over budget=%u/%u, "
                    "max work=%.1f ms, max start lag=%.1f ms\n",
                    lastMeasuredTps_, simulationPacing_.ticksPerSecond(),
                    elapsedMillis / 1000.0, players, tickCadenceOverBudget_, tickCadenceSamples_,
                    tickCadenceMaxWorkMillis_, tickCadenceMaxLagMillis_);
        std::printf("[tick] load players=%zu, sessions=%zu, entities=%zu, mobs=%zu, pets=%zu, "
                    "drops=%zu, projectiles=%zu, archetypes=%zu, rss=%.1f MiB\n",
                    players, sessions_.size(), world_.size(), Query<MobTag>{world_}.count(),
                    Query<Pet>{world_}.count(), Query<DropTag>{world_}.count(),
                    Query<ProjectileTag>{world_}.count(), world_.archetypeCount(),
                    residentBytes() / (1024.0 * 1024.0));
        std::fflush(stdout);
    }
    tickCadenceStartMillis_ = nowMillis;
    tickCadenceSamples_ = 0;
    tickCadenceOverBudget_ = 0;
    tickCadenceMaxWorkMillis_ = 0;
    tickCadenceMaxLagMillis_ = 0;
}

void GameServer::recordTickProbe(double tickNow, double timerLag, double networkBefore, double startLag,
                                 double work, double cpu, double wallStart, int previousRate, int rate) {
    auto& window = tickProbeWindow_;
    const bool hasWindow = tickCadenceStartMillis_ > 0.0;
    if (hasWindow) {
        ++window.targets[previousRate];
        window.overNominal += work >= net::kTickMillis;
        window.overTarget += work >= 1000.0 / previousRate;
        window.late += startLag >= 50.0;
        window.transitions += previousRate != rate;
    }
    const bool lagPeak = hasWindow && startLag > window.maxLag;
    const bool workPeak = hasWindow && work > window.maxWork;
    const double sessions = sessionPhaseCount_
        ? sessionPhases_[sessionPhaseCount_ - 1].endedMillis - tickStartedMillis_ : 0.0;
    const bool sessionsPeak = hasWindow && sessions > window.maxSessions;
    const bool transition = previousRate != rate;
    const bool save = saveProbe_.periodic || saveProbe_.poll.wall >= 25.0;
    const bool contactVisitFrame = contactVisitFrame_ != ContactVisitFrame::None;
    if (lagPeak || workPeak || sessionsPeak || transition || save || contactVisitFrame) {
        Json frame = Json::object();
        frame["tick"] = static_cast<double>(tick_);
        frame["wallStartMs"] = wallStart;
        frame["players"] = static_cast<double>(playerCount());
        frame["timerLagMs"] = timerLag;
        frame["networkBeforeMs"] = networkBefore;
        frame["startLagMs"] = startLag;
        frame["workMs"] = work;
        frame["threadCpuMs"] = cpu;
        frame["targetBefore"] = previousRate;
        frame["targetAfter"] = rate;
        frame["workEwmaMs"] = simulationPacing_.meanWorkMillis();
        Json stages = Json::object();
        double previous = tickStartedMillis_;
        for (std::size_t i = 0; i < tickPhaseCount_; ++i) {
            const auto& phase = tickPhases_[i];
            stages[phase.name] = phase.endedMillis - previous;
            previous = phase.endedMillis;
        }
        if (stages.contains("contact")) {
            const auto& timing = combat_->contactTiming();
            Json contact = Json::object();
            contact["beginTickMs"] = contactBeginMs_;
            contact["setupMs"] = timing.setupMs;
            Json setup = Json::object();
            setup["aurasMs"] = timing.aurasMs;
            setup["gatherBodiesMs"] = timing.gatherBodiesMs;
            setup["hostileGridMs"] = timing.hostileGridMs;
            setup["gatherPetalsMs"] = timing.gatherPetalsMs;
            setup["fileShotsMs"] = timing.fileShotsMs;
            contact["prepareMs"] = std::move(setup);
            contact["densityMs"] = timing.densityMs;
            contact["residualMs"] = stages["contact"].asDouble() - contactBeginMs_ - timing.setupMs
                - timing.parallelSearchMs - timing.fallbackSearchMs - timing.orderedMs - timing.densityMs;
            contact["visitSampleStride"] = static_cast<double>(CombatSystem::kContactVisitSampleStride);
            contact["sampleClockNs"] = timing.sampleClockNs;
            contact["parallelSearchMs"] = timing.parallelSearchMs;
            contact["fallbackSearchMs"] = timing.fallbackSearchMs;
            contact["orderedMs"] = timing.orderedMs;
            contact["planned"] = static_cast<double>(timing.planned);
            contact["fallback"] = static_cast<double>(timing.fallback);
            contact["hotspotRadius"] = CombatSystem::kProbeHotspotRadius;
            Json maps = Json::array();
            constexpr std::array<const char*, 5> kinds{"players", "mobs", "petals", "shots", "pets"};
            constexpr std::array<const char*, static_cast<std::size_t>(CombatSystem::ContactVisitPhase::Count)>
                visitPhases{"lookup", "geometry", "eligibility", "formula", "writeback", "pvp"};
            for (std::size_t i = 0; i < timing.realms.size(); ++i) {
                const auto& map = timing.realms[i];
                if (!map.sources && !map.total[3]) continue;
                Json row = Json::object(), local = Json::object();
                row["realm"] = static_cast<double>(i);
                if (const auto* data = worldMaps_.forRealm(static_cast<Realm>(i))) {
                    row["map"] = data->templateId();
                    row["prestige"] = data->prestige();
                }
                row["sources"] = static_cast<double>(map.sources);
                row["raw"] = static_cast<double>(map.raw);
                row["visits"] = static_cast<double>(map.visits);
                row["nonPvpOverlaps"] = static_cast<double>(map.overlaps);
                row["peak"] = static_cast<double>(map.peak);
                if (map.sampled.visits) {
                    row["sampledVisits"] = static_cast<double>(map.sampled.visits);
                    Json ns = Json::object(), intervals = Json::object();
                    for (std::size_t phase = 0; phase < visitPhases.size(); ++phase) {
                        ns[visitPhases[phase]] = map.sampled.ms[phase] * 1e6 / map.sampled.visits;
                        intervals[visitPhases[phase]] = static_cast<double>(map.sampled.intervals[phase]);
                    }
                    row["sampledNsPerVisit"] = std::move(ns);
                    row["sampledIntervals"] = std::move(intervals);
                }
                for (std::size_t kind = 0; kind < kinds.size(); ++kind) {
                    row[kinds[kind]] = static_cast<double>(map.total[kind]);
                    local[kinds[kind]] = static_cast<double>(map.local[kind]);
                }
                if (map.peak) {
                    row["cellX"] = grid_.cellX(static_cast<Realm>(i), map.hotspot.x);
                    row["cellY"] = grid_.cellY(static_cast<Realm>(i), map.hotspot.y);
                    row["near"] = std::move(local);
                }
                maps.push(std::move(row));
            }
            contact["maps"] = std::move(maps);
            frame["contact"] = std::move(contact);
        }
        if (stages.contains("mob movement")) {
            const auto& timing = movement_->lastWorldPhaseTiming();
            Json movement = Json::object();
            movement["mobStepsMs"] = timing.mobStepsMs;
            movement["mobStepJoinMs"] = timing.mobStepJoinMs;
            movement["separationSolveMs"] = timing.separationSolveMs;
            movement["separationJoinMs"] = timing.separationJoinMs;
            frame["mobMovementMs"] = std::move(movement);
        }
        frame["stagesMs"] = std::move(stages);
        frame["stageOtherMs"] = std::max(0.0, work - (previous - tickStartedMillis_));
        Json sessionStages = Json::object();
        previous = tickStartedMillis_;
        for (std::size_t i = 0; i < sessionPhaseCount_; ++i) {
            const auto& phase = sessionPhases_[i];
            sessionStages[phase.name] = phase.endedMillis - previous;
            previous = phase.endedMillis;
        }
        frame["sessionsMs"] = std::move(sessionStages);
        const auto timingJson = [](const ProbeTiming& timing) {
            Json result = Json::object();
            result["wallMs"] = timing.wall;
            result["threadCpuMs"] = timing.cpu;
            return result;
        };
        Json persistence = Json::object();
        persistence["periodic"] = saveProbe_.periodic;
        persistence["capture"] = timingJson(saveProbe_.capture);
        persistence["prune"] = timingJson(saveProbe_.prune);
        persistence["request"] = timingJson(saveProbe_.request);
        persistence["poll"] = timingJson(saveProbe_.poll);
        persistence["other"] = timingJson(saveProbe_.other);
        Json rowEdits = Json::object();
        rowEdits["attempts"] = static_cast<double>(saveProbe_.sessionsEdits.attempts);
        rowEdits["copies"] = static_cast<double>(saveProbe_.sessionsEdits.copies);
        rowEdits["wallMs"] = saveProbe_.sessionsEdits.wallMs;
        persistence["sessionsSnapshotEdits"] = std::move(rowEdits);
        frame["save"] = std::move(persistence);
        const auto line = frame.dump();
        if (contactVisitFrame) {
            const char* label = contactVisitFrame_ == ContactVisitFrame::Trigger ? "contact-trigger" : "contact-sample";
            std::printf("[tick-probe-frame] %s %s\n", label, line.c_str());
        } else if (transition || save) {
            std::printf("[tick-probe-frame] %s %s\n", transition ? "transition" : "save", line.c_str());
        }
        if (lagPeak) {
            window.maxLag = startLag;
            window.lagTick = tick_;
            window.lagFrame = line;
        }
        if (workPeak) {
            window.maxWork = work;
            window.workTick = tick_;
            window.workFrame = line;
        }
        if (sessionsPeak) {
            window.maxSessions = sessions;
            window.sessionsTick = tick_;
            window.sessionsFrame = line;
        }
    }
    if (!hasWindow || tickNow - tickCadenceStartMillis_ < kTickCadenceLogMillis) return;
    std::printf("[tick-probe-window] targets=20:%u,16:%u,13:%u,11:%u,10:%u over50=%u overTarget=%u late50=%u transitions=%u ewma=%.1f\n",
                window.targets[20], window.targets[16], window.targets[13], window.targets[11],
                window.targets[10], window.overNominal, window.overTarget, window.late,
                window.transitions, simulationPacing_.meanWorkMillis());
    if (window.lagTick == window.workTick) {
        std::printf("[tick-probe-frame] window-lag-work %s\n", window.lagFrame.c_str());
    } else {
        std::printf("[tick-probe-frame] window-lag %s\n", window.lagFrame.c_str());
        std::printf("[tick-probe-frame] window-work %s\n", window.workFrame.c_str());
    }
    if (!window.sessionsFrame.empty() &&
        window.sessionsTick != window.lagTick && window.sessionsTick != window.workTick) {
        std::printf("[tick-probe-frame] window-sessions %s\n", window.sessionsFrame.c_str());
    }
    window = {};
}

void GameServer::markTickPhase(const char* name) {
    if (tickPhaseCount_ < tickPhases_.size()) {
        tickPhases_[tickPhaseCount_++] = TickPhase{name, monotonicMillis()};
    }
}

std::string GameServer::describeTickPhases(double tookMillis) const {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "[tick] slow tick %.1f ms:", tookMillis);
    std::string line = buffer;
    double from = tickStartedMillis_;
    bool any = false;
    for (std::size_t i = 0; i < tickPhaseCount_; ++i) {
        const double took = tickPhases_[i].endedMillis - from;
        from = tickPhases_[i].endedMillis;
        // The phases that cost nothing are most of them, and listing them
        // buries the one that did.
        if (took < 1.0) continue;
        std::snprintf(buffer, sizeof buffer, "%s %s %.1f", any ? "," : "", tickPhases_[i].name,
                      took);
        line += buffer;
        any = true;
    }
    if (!any) line += " no phase over 1 ms";
    return line;
}

void GameServer::tick(double nowMillis) {
    tick(nowMillis, nowMillis);
}

void GameServer::tick(double wallMillis, double nowMillis) {
    if (persistenceFrozen_ || (pendingLocalHyper_ && pendingLocalHyper_->publishing)) return;
    ++tick_;
    wallClockMillis_ = wallMillis;
    clockMillis_ = nowMillis;
    tickPhaseCount_ = 0;
    contactVisitFrame_ = ContactVisitFrame::None;
    sessionPhaseCount_ = 0;
    if (!bossClocksRestored_) restoreBossClocks(nowMillis);
    tickStartedMillis_ = monotonicMillis();
    const auto editsBeforeSessions = config_.tickProbe ? database_.snapshotEdits()
        : Database::SnapshotEditTiming{};
    const auto markSessionPhase = [&](const char* name) {
        if (config_.tickProbe && sessionPhaseCount_ < sessionPhases_.size())
            sessionPhases_[sessionPhaseCount_++] = {name, monotonicMillis()};
    };
    servicePyramidRotation(pyramidRotationCycle());
    markSessionPhase("pyramidRotation");

    // First, so a session whose socket is gone is not counted as a player,
    // simulated or saved by anything below. Above the idle gate because such a
    // session is exactly what keeps a server from ever going idle.
    if (wallMillis >= nextSocketAuditMillis_) {
        nextSocketAuditMillis_ = wallMillis + 1000.0;
        reapSessionsWithoutSockets();
    }
    markSessionPhase("socketAudit");

    if (config_.sharedSkinRealm && wallMillis >= nextSharedSkinExpiryCheck_) {
        nextSharedSkinExpiryCheck_ = wallMillis + 10'000.0;
        expireSharedSkinSources();
    }

    for (auto& entry : sessions_) refillAllowances(entry.second, wallMillis);
    markSessionPhase("allowances");
    // At most one credited second per account and server second; idle input heartbeats do not count.
    if (wallMillis >= nextCouncilSampleMillis_) {
        nextCouncilSampleMillis_ = wallMillis + 1000.0;
        std::unordered_map<std::string,bool> credited;
        for(auto& entry:sessions_) {
            auto& s=entry.second;
            const double elapsed=s.councilSampleAt>0?std::min(1.0,std::max(0.0,(wallMillis-s.councilSampleAt)/1000)):0;
            s.councilSampleAt=wallMillis;
            if (!s.playing() || s.councilActiveUntil < wallMillis) continue;
            const auto* a = std::as_const(database_).findUserById(s.userId);
            if (!a || a->guest || a->admin || credited[s.userId]) continue;
            credited[s.userId]=true;
            // This counter belongs to the exclusively leased account. The old
            // global table is a read-only historical baseline: a shard's stale
            // copy must never overwrite another shard's activity or block saves.
            // Apply these seconds after the forge finishes so an activity
            // sample cannot invalidate the two inventory snapshots in flight.
            if (pendingLocalHyper_ && pendingLocalHyper_->expectedPlayers.count(s.userId)) {
                pendingLocalHyper_->councilActivityCredits[s.userId] += elapsed;
            } else {
                auto& activity = database_.progress(s.userId).extra["councilActiveSeconds"];
                activity = std::max(0.0, activity.asDouble()) + elapsed;
                database_.markDirty();
            }
        }
    }
    markSessionPhase("council");

    // Invitations lapse on the server's own clock rather than on a timer per
    // invite, which is a timer that has to be cancelled when its target
    // disconnects. The ranking table is rebuilt beside it because a member's
    // BODY changes on every death: cached against the roster it would go stale
    // without the roster ever moving.
    squads_.expire(static_cast<std::int64_t>(wallMillis));
    rebuildSquadIndex();
    markSessionPhase("squads");

    // Above the idle gate on purpose: a server with nobody left on it is
    // exactly the one a scheduled restart is usually waiting for, and an
    // install that finished while the last player left still has a restart to
    // schedule.
    serviceAutoUpdate();
    serviceScheduledRestart(wallMillis);
    markSessionPhase("maintenance");
    // Above the idle gate too: a member logging out of the title screen
    // changes every guildmate's roster whether or not anybody is playing.
    serviceGuildPresence(wallMillis);
    markSessionPhase("guildPresence");
    // Public online list under the minimap (same session walk as the admin
    // crown Players tab; fingerprint skips unchanged payloads).
    serviceOnlineRoster(wallMillis);
    markSessionPhase("rosterBuild");
    sendDueOnlineRosters();
    markSessionPhase("rosterSend");
    serviceTimedHammers(wallMillis);
    markSessionPhase("timedHammers");
    // One-shot requests are staged only by the server operator in the verified
    // stopped database. No client packet or chosen player name can enqueue one.
    // The maintenance marker keeps the transaction behind successful cutover.
    {
        const Json& job=database_.storedTable("operatorHyperForgeRequest");
        const auto status=job["status"].asString();
        const bool needsWork=status=="pending" ||
            (status=="done" && !job["announced"].asBool(false));
        const auto userId=job["userId"].asString();
        const auto requestId=job["id"].asString();
        const auto waiting=[&job] {
            const auto gate=job["maintenanceGate"].asString();
            const auto ready=job["readyFile"].asString();
            return (!gate.empty()&&std::ifstream(gate).good()) ||
                (!ready.empty()&&!std::ifstream(ready).good());
        };
        if(needsWork&&!config_.authorityHyper&&!userId.empty()&&!requestId.empty()&&!waiting()) {
            if(status=="pending") {
                const auto& state=database_.storedTable("hyperForges")["mjolnir"];
                const bool completed=state["operatorRequest"].asString()==requestId&&
                    state["owner"].asString()==userId;
                HyperForgeResult result;
                if(completed){result.ok=true;result.consumed=state["paid"].asInt(5);}
                else if(database_.findUserById(userId)) {
                    persistAll();
                    if(job["mode"].asString()=="temporary15h")
                        result=grantTemporaryHyperHammer(database_,userId,database_.nowMillis(),requestId);
                    else result=forgeHyper(database_,userId,"fragment",database_.nowMillis(),requestId);
                }else result.reason="指定账号不存在，未兑换。";
                auto& receipt=database_.rawTable("operatorHyperForgeRequest");
                receipt["status"]=result.ok?"done":"failed";
                receipt["reason"]=result.reason;
                receipt["consumed"]=result.consumed;
                receipt["completedAt"]=Json(static_cast<double>(database_.nowMillis()));
                database_.markDirty();database_.save();
                if(result.ok)for(auto& [id,other]:sessions_){
                    if(!other.authenticated()||std::find(result.affected.begin(),result.affected.end(),other.userId)==result.affected.end())continue;
                    if(other.playing())applyAccountToSession(other);
                    if(auto* peer=listener_.find(id))sendProfile(other,*peer);
                }
            }
            const auto& receipt=database_.storedTable("operatorHyperForgeRequest");
            if(receipt["status"].asString()=="done"&&!receipt["announced"].asBool(false)) {
                bool listeners=false;for(const auto& [id,peer]:sessions_)listeners=listeners||peer.authenticated();
                const Account* account=database_.findUserById(userId);
                if(listeners&&account){
                    Session credit;credit.userId=userId;credit.username=account->username;
                    const bool temporary=receipt["mode"].asString()=="temporary15h";
                    announceRareCraft(credit,content().petalIndex("mjolnir"),Rarity::Apex,1,temporary?0:receipt["consumed"].asInt(5),true);
                    if(temporary)broadcastChat(net::ChatChannel::System,"",account->username+" 获得补发的超凡锤子，限时15小时；未消耗永恒锤子。 ");
                    database_.rawTable("operatorHyperForgeRequest")["announced"]=true;
                    database_.markDirty();database_.save();
                }
            }
        }
    }
    markSessionPhase("operatorJob");
    markTickPhase("sessions");

    // Housekeeping, ABOVE the idle gate: an account registered by somebody
    // sitting on the title screen is dirty in memory and would otherwise wait
    // for the next player to actually join before it reached the disk.
    const bool checkpoint = wallMillis >= nextPersistMillis_;
    const bool traceSave = config_.tickProbe;
    const double saveStart = traceSave ? tickPhases_[tickPhaseCount_ - 1].endedMillis : 0.0;
    const double saveCpu = traceSave ? tickProbeThreadCpuMillis() : -1.0;
    if (traceSave) {
        saveProbe_ = {};
        saveProbe_.periodic = checkpoint;
        const auto after = database_.snapshotEdits();
        saveProbe_.sessionsEdits = {after.attempts - editsBeforeSessions.attempts,
            after.copies - editsBeforeSessions.copies, after.wallMs - editsBeforeSessions.wallMs};
    }
    const auto measureSave = [&](ProbeTiming& timing, auto&& operation) {
        if (!traceSave) { operation(); return; }
        const double start = monotonicMillis();
        const double cpuStart = tickProbeThreadCpuMillis();
        operation();
        timing.wall = monotonicMillis() - start;
        timing.cpu = tickProbeCpuElapsed(cpuStart);
    };
    if (checkpoint) {
        nextPersistMillis_ = wallMillis + kPersistIntervalMillis * (config_.loadShedding ? 2.0 : 1.0);
        measureSave(saveProbe_.capture, [&] { captureLiveProgress(); });
        measureSave(saveProbe_.prune, [&] { database_.pruneExpiredSessions(); });
        measureSave(saveProbe_.request, [&] { saveDatabase(true); });
    }
    measureSave(saveProbe_.poll, [&] { database_.pollBackgroundSave(); });
    markTickPhase(database_.backgroundSaveEnabled() ? "save handoff" : "persist");
    if (traceSave) {
        const auto& trace = saveProbe_;
        saveProbe_.other.wall = std::max(0.0, tickPhases_[tickPhaseCount_ - 1].endedMillis - saveStart
            - trace.capture.wall - trace.prune.wall - trace.request.wall - trace.poll.wall);
        const double totalCpu = tickProbeCpuElapsed(saveCpu);
        if (totalCpu >= 0.0)
            saveProbe_.other.cpu = std::max(0.0, totalCpu - std::max(0.0, trace.capture.cpu)
                - std::max(0.0, trace.prune.cpu) - std::max(0.0, trace.request.cpu) - std::max(0.0, trace.poll.cpu));
    }

    // Bot population is maintained on every tick, INCLUDING the idle ones the
    // gate below returns out of -- that is where the reference calls it, and
    // it is what lets an empty server retire its bots after the grace period
    // rather than leaving them simulating for nobody.
    maintainBots(nowMillis);
    markTickPhase("bot population");

    // Nobody in the world means nothing to simulate. The reference returns out
    // of its tick here and the whole world freezes: mobs stop wandering, nests
    // stop firing, lifetimes stop burning down and the unseen-despawn census
    // never runs. Without this gate an idle server quietly empties itself of
    // every mob it had and the first player back arrives on a bare map.
    //
    // The command buffer is still flushed: a disconnect between ticks queues a
    // destroy, and leaving it queued would keep the departed body in the world
    // and in every query until somebody joined.
    if (playerCount() == 0) {
        events_.clear();
        commands_.flush();
        listener_.flush();
        return;
    }

    runSystems(nowMillis, smoothedDeltaSeconds_);
    persistBossClocks();
    servicePyramidMaps(nowMillis);
    serviceSacrificeSpawn(nowMillis);
    serviceSacrificeRituals(nowMillis);

    // BEFORE the reaper. A splitter gives one connection two bodies, and a
    // half that died this tick has to stop being part of the session before
    // the reaper meets it -- otherwise its owner is shown a death card for a
    // flower they are no longer steering. It is also where the split follows
    // the loadout: equipping the petal splits, taking it off merges.
    serviceSplitters(nowMillis);
    bankExperience();
    markTickPhase("splitters");

    // After the splitters, which may have moved a controlled flower onto its
    // other half, and before the reaper for the splitters' own reason: a death
    // this tick ends the control first, so the admin's view is back on their
    // own flower before either corpse is announced.
    serviceControl();
    markTickPhase("control");

    reapDead();
    commands_.flush();
    serviceNearbyRevives(nowMillis);
    markTickPhase("reap");
    loot_->pruneGroundDrops(world_);
    markTickPhase("drop cap");

    // The wire runs slower than the simulation and on its own clock: physics
    // and combat want 30 Hz, clients do not, and the per-recipient encode/cull/
    // delta pass is the largest thing in the tick that nothing simulated
    // depends on. Nothing about WHAT happens changes -- one-shot events are
    // queued for the frame either way -- only how often it is described.
    if (wallMillis >= nextSnapshotMillis_) {
        // Stepped from the deadline, not from now, so the send rate does not
        // slew with tick jitter; resynced when it falls a whole interval behind
        // rather than firing a burst of catch-up frames.
        nextSnapshotMillis_ += net::kSnapshotMillis;
        if (nextSnapshotMillis_ < wallMillis) nextSnapshotMillis_ = wallMillis + net::kSnapshotMillis;
        sendDueProfiles();
        markTickPhase("profiles");
        replicate(nowMillis, wallMillis);
        markTickPhase("replicate");
        // One-shot events ride inside the snapshot here, where the reference
        // has its own per-tick outbox, so they are BANKED across the ticks that
        // send nothing rather than dropped: a hit that landed on a simulation
        // tick with no snapshot must still produce its number.
        events_.clear();
    }
    listener_.flush();
    markTickPhase("send");
}

void GameServer::runSystems(double nowMillis, double dt) {
    if (!workers_) {
        const unsigned simulation = ServerWorkers::configuredLimit("FLIX_SIMULATION_WORKERS", 7);
        const unsigned snapshots = ServerWorkers::configuredLimit("FLIX_SNAPSHOT_WORKERS");
        workers_ = std::make_unique<ServerWorkers>(std::max(simulation, snapshots));
        mobAi_->workers = movement_->workers = loot_->workers =
            simulation ? workers_.get() : nullptr;
        combat_->workers = mobAi_->workers;
        mobAi_->workerLimit = movement_->workerLimit = loot_->workerLimit = simulation;
        combat_->workerLimit = simulation;
        parallelSnapshots_ = std::make_unique<ParallelSnapshots>(*workers_, snapshots);
        std::printf("[workers] pool=%u simulation=%u snapshots=%u contact=%u\n", workers_->count(),
                    std::min(simulation, workers_->count()), std::min(snapshots, workers_->count()),
                    std::min(combat_->workerLimit, workers_->count()));
    }
    // Order matters and is the tick's whole contract:
    //   bot intent -> players -> petals -> mob intent/movement -> projectiles
    //   -> combat -> spawning -> loot.
    // The TypeScript server closed the player movement window and ran the
    // player's petal pipeline before moveEnemies(), then advanced projectiles
    // after the mobs, and this keeps that order.
    //
    // `dt` is the smoothed real step and drives the dt-SCALED half: flowers,
    // petals, combat's timed effects (afflictions, ground fields) and the
    // drops' lifetimes. The mob half is a FIXED per-call step, as it was in
    // the TypeScript server (src/server.ts:1321 handed moveEnemies a hard
    // 1/30), and projectile FLIGHT moves with it -- MovementSystem's world
    // phase steps the mobs and then the shots. So the mob AI, that phase and
    // the spawner are given net::kTickSeconds explicitly below rather than the
    // tick's delta.

    // Bots decide before anything moves, which is where the reference samples
    // input: their decisions are made against the world as this tick found it
    // and are consumed by the very next stage, not one tick later.
    stepBots(nowMillis);
    markTickPhase("bot ai");

    // Modifiers before movement, as the reference schedules them (its
    // playerModifiers system sits in Phase.Input, ahead of playerMovement).
    // Folded only inside the petal phase below, a speed or size petal would not
    // reach movement until the tick after it was equipped.
    petals_->foldModifiers(world_, content(), nowMillis);

    const double controlSeconds = simulationPacing_.ticksPerSecond() < net::kTicksPerSecond
                                      ? simulationPacing_.intervalMillis() / 1000.0 : 0.0;
    movement_->runPlayerPhase(world_, *terrain_, nowMillis, dt, controlSeconds);
    markTickPhase("players");
    petals_->run(world_, content(), nowMillis, dt, commands_, terrain_.get(), &events_);
    markTickPhase("petals");

    // Mob targeting must see the flowers' newly committed positions. Refresh
    // both the LOD list and broadphase after player movement instead of asking
    // AI to make this tick's decision from last tick's coordinates.
    //
    // Two lists, because two questions are being asked. EVERY flower is an
    // observer for the mob LOD and for the spawner's wake-up pass, bots
    // included: a bot fighting a mob is something worth simulating properly,
    // and a bot standing in a band is something that band has to be awake for.
    // A bot in that list can no longer inflate anything -- a band holds the
    // population its own area buys whether anyone is there or not, and being
    // looked at only decides how much of it is currently an entity.
    //
    // The HUMAN list is what the arena and the maze are filled for: those are
    // generated realms with no bands, ModeSpawner stocks each one whole while
    // somebody is in it, and a bot has no business keeping one populated.
    activePlayers_.clear();
    Query<PlayerTag, Transform> players{world_};
    players.each([&](Entity, PlayerTag&, Transform& transform) {
        activePlayers_.push_back({transform.position, transform.realm});
    });
    humanPlayers_.clear();
    for (const auto& entry : sessions_) {
        if (!entry.second.playing()) continue;
        if (const Transform* transform = world_.tryGet<Transform>(entry.second.entity)) {
            humanPlayers_.push_back({transform->position, transform->realm});
        }
    }
    grid_.clear();
    mobTargetGrid_.clear();
    playerTargetGrid_.clear();
    // Every body but the projectiles, which combat files in a broadphase of
    // its own (CombatSystem::fileShots): nothing else that asks this grid
    // wants a shot, and a flower on a full loadout of gas would otherwise put
    // a thousand of them into every aggro scan and pickup query around it.
    //
    // Nor an intangible mob: what nothing may touch is simply not in the grid
    // anything touches through, so no contact, petal, aura, strike, pet or
    // bot ever finds it to ask.
    Query<Transform, Body> afterPlayers{world_};
    // Loot only uses the later grid. Its stationary drops cannot take part
    // in the contact and AI queries that run against this one.
    afterPlayers.without<ProjectileTag, Intangible, DropTag>();
    contactBodies_.clear();
    afterPlayers.each([&](Entity e, Transform& transform, Body& body) {
        grid_.insert(e, transform.realm, transform.position, body.radius);
        contactBodies_.push_back({e, transform.realm, transform.position, body.radius});
        // Wild mobs look for flowers; summoned mobs look for wild mobs or
        // training posts. Both scans used to walk every petal and summon in
        // a crowded cell before finding those candidates. Use the same
        // cells and insertion order to preserve equal-score target choices.
        const bool isPlayer = world_.has<PlayerTag>(e);
        if (isPlayer) playerTargetGrid_.insert(e, transform.realm, transform.position, body.radius);
        if (isPlayer ||
            ((world_.has<MobTag>(e) || world_.has<Npc>(e)) && !world_.has<Pet>(e))) {
            mobTargetGrid_.insert(e, transform.realm, transform.position, body.radius);
        }
    });
    markTickPhase("broadphase");

    // The reference server resolves flower bodies and the petal ring inside
    // the player pipeline, before moveEnemies(). Keep that temporal boundary:
    // a mob cannot escape a petal it was already touching by moving first.
    const double contactStart = config_.tickProbe ? monotonicMillis() : 0.0;
    const bool sampleContact = contactVisitTicksLeft_ > 0;
    const double contactPhaseStart = config_.contactVisitProbe ? tickPhases_[tickPhaseCount_ - 1].endedMillis : 0.0;
    combat_->beginTick(world_, nowMillis, dt, events_);
    if (config_.tickProbe) contactBeginMs_ = monotonicMillis() - contactStart;
    combat_->runContactPhase(world_, grid_, content(), nowMillis, &contactBodies_, config_.tickProbe, sampleContact);
    markTickPhase("contact");
    if (sampleContact) {
        --contactVisitTicksLeft_;
        contactVisitFrame_ = ContactVisitFrame::Sample;
    } else if (config_.contactVisitProbe && contactPhaseStart >= nextContactVisitBurstMs_ &&
               tickPhases_[tickPhaseCount_ - 1].endedMillis - contactPhaseStart >= 30.0) {
        // The trigger has already run. Sample its successors without replaying a hit.
        contactVisitFrame_ = ContactVisitFrame::Trigger;
        contactVisitTicksLeft_ = 4;
        nextContactVisitBurstMs_ = contactPhaseStart + 10000.0;
    }

    mobAi_->run(world_, *terrain_, mobTargetGrid_, activePlayers_, nowMillis,
                net::kTickSeconds, commands_, &playerTargetGrid_);
    markTickPhase("mob ai");
    movement_->runWorldPhase(world_, *terrain_, nowMillis, net::kTickSeconds, config_.tickProbe);
    markTickPhase("mob movement");
    // After the bodies have moved, as gardn places its segments: whatever
    // pushed a bead this tick swings the chain rather than being undone.
    mobAi_->placeChains(world_, *terrain_, activePlayers_);
    // AFTER the mobs have moved, for the reason a flower's ring is placed
    // after its own movement: a seat is a rigid offset from the body, and a
    // ring carried before the body moves trails it by a tick.
    mobAi_->tickPetalRings(world_, commands_);

    // Combat exact-tests current transforms, but its candidate set comes from
    // this grid. Rebuild after mob/projectile flight so a cell crossing cannot
    // make a real overlap invisible for one tick.
    grid_.clear();
    Query<Transform, Body> afterMovement{world_};
    afterMovement.without<ProjectileTag, Intangible, DropTag>();
    afterMovement.each([&](Entity e, Transform& transform, Body& body) {
        grid_.insert(e, transform.realm, transform.position, body.radius);
    });
    markTickPhase("regrid");
    combat_->runWorldPhase(world_, grid_, content(), nowMillis, dt, true);
    // A summoning shot -- the spaceship's -- is only SEEN landing by combat;
    // the summon is the ring's, made once combat has stopped walking the world.
    petals_->landShots(world_, content(), combat_->shotLandings(), terrain_.get());
    servicePracticeTrials(nowMillis);
    markTickPhase("combat");
    spawning_->run(world_, *terrain_, content(), activePlayers_, rng_, nowMillis,
                   net::kTickSeconds, commands_);
    // What this tick's dead leave behind -- a tomb's mummy, its opened shell
    // -- read while combat's corpses are still in the world.
    {
        std::vector<Entity> deadMobs;
        for (const CombatSystem::DeathRecord& death : combat_->deaths()) {
            if (!death.wasPlayer) deadMobs.push_back(death.entity);
        }
        spawning_->answerDeaths(world_, *terrain_, content(), deadMobs, rng_, nowMillis,
                                net::kTickSeconds, commands_);
    }
    markTickPhase("spawning");
    // After combat, so a dungeon cleared this tick drops its nest this tick,
    // and before loot, so the nest's drops are rolled in the same pass.
    dungeons_->run(world_, *terrain_, content(), *spawning_, *combat_, rng_, nowMillis,
                   commands_);
    // The arena and the maze are filled whole rather than by viewport, and only
    // while someone is in them.
    modes_->run(world_, *terrain_, content(), *spawning_, grid_, humanPlayers_, rng_, nowMillis);
    // Every flower, bots included, is somebody an NPC can turn to look at.
    npcs_->run(world_, *terrain_, content(), activePlayers_, nowMillis, wallClockMillis_);
    markTickPhase("modes+npcs");
    // Pickups can only collect drops. Keeping them out of the body grid
    // also keeps stationary loot out of projectile and aura queries.
    pickupGrid_.clear();
    Query<DropTag, Transform, Body>{world_}.each(
        [&](Entity e, DropTag&, Transform& transform, Body& body) {
            pickupGrid_.insert(e, transform.realm, transform.position, body.radius);
        });
    loot_->run(world_, pickupGrid_, content(), rng_, nowMillis, dt, commands_, events_);
    markTickPhase("loot");

    // A pickup is a world event; owning it is an account fact. The loot system
    // deliberately knows nothing about the database, so the hand-off is here.
    bankPickups();
    // Same reasoning for kills: combat marks the corpse, the account keeps the
    // tally. Runs before the reaper, while MobType is still readable.
    bankKills();

    // The spawner has no view of the socket list, so it queues the bosses it
    // admitted rather than announcing them.
    announceBossSpawns();
    markTickPhase("banking");
}

void GameServer::announceBossSpawns() {
    for (const SpawnSystem::BossSpawn& boss : spawning_->bossSpawns) {
        // The bots hear about it too, from the same event and in the same
        // breath. Every path that can put a boss in the world queues one of
        // these -- a band stocking itself in a corner nobody has visited, a
        // nest's escort, the arena, an operator's console -- so there is one
        // place a boss becomes news and no way to add a spawn path that
        // produces one the server says nothing about.
        noteBossSighting(boss.entity, clockMillis_);
        // A sacrifice's mob was announced as summoned by the ritual that
        // brought it (announceSacrificeSummon), at any tier.
        const Replicated* replicated = world_.tryGet<Replicated>(boss.entity);
        if (replicated != nullptr && (replicated->spawnFlags & net::SpawnSacrificed) != 0) {
            continue;
        }

        announceMob(boss.mobIndex, boss.rarity, boss.realm, false);
    }
    spawning_->bossSpawns.clear();
}

void GameServer::announceMob(std::uint16_t mobIndex, Rarity rarity,
                             std::optional<Realm> localRealm, bool summoned) {
    if (mobIndex >= content().mobCount()) return;
    char color[16];
    std::snprintf(color, sizeof color, "#%06x", rarityColor(rarity));
    const auto prefix = std::string("<b style=\"color: ") + color + ";\">A " +
        rarityLabel(rarity) + " " + content().mob(mobIndex).name +
        (summoned ? " has been summoned" : " has spawned");
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.playing()) continue;
        net::Connection* connection = listener_.find(session.connection);
        if (connection == nullptr) continue;
        const Transform* transform = world_.tryGet<Transform>(session.entity);
        const bool here = localRealm && transform && transform->realm == *localRealm;
        sendChatTo(*connection, net::ChatChannel::System, "",
                   prefix + (here ? "" : " somewhere") + "!</b>");
    }
    // Only the originating world publishes. Receiving worlds never learn the realm.
    if (localRealm && config_.authorityAuth) {
        Json event = Json::object();
        event["action"] = "mobAnnouncement";
        event["mob"] = int(mobIndex);
        event["rarity"] = rarityIndex(rarity);
        event["summoned"] = summoned;
        config_.authorityAuth(event);
    }
}

void GameServer::bankPickups() {
    for (const LootSystem::Pickup& pickup : loot_->pickups()) {
        Session* session = sessionForEntity(pickup.player);
        if (!session || session->userId.empty()) continue;
        if (pickup.petalIndex >= content().petalCount()) continue;

        // Into the bag the body is playing with: an arena pickup is the run's,
        // not the account's, until the run ends.
        auto& record = liveRecord(*session);
        const auto key = inventoryKey(pickup.petalIndex);
        const int previous = record.itemCount(pickup.rarity, key);
        const auto credited = std::min(pickup.quantity, static_cast<std::uint32_t>(
            kMaxStackCount - previous));
        if (credited > 0) {
            record.setItemCount(pickup.rarity, key, previous + static_cast<int>(credited));
            auto& total = session->runLoot[{pickup.petalIndex, pickup.rarity}];
            total += std::min(credited, UINT32_MAX - total);
        }
        database_.markDirty();

        // Owed, not sent: see sendDueProfiles.
        noteDue(session->stacksDue, pickup.petalIndex, pickup.rarity);
        session->profileDue = true;
        announceRarePickup(*session, pickup.petalIndex, pickup.rarity);
    }
}

void GameServer::reapDead() {
    // Death is a component, not a destroy, so everything later in the SAME tick
    // still sees the entity -- a mob that dies during combat must still be
    // there for the loot system to read its contributor list. The actual
    // destroy happens here, once, at the end.
    Query<Dead> dead{world_};
    std::vector<Entity> doomed;
    dead.collect(doomed);
    for (const Entity e : doomed) {
        const bool isPlayer = world_.has<PlayerTag>(e);
        Session* session = isPlayer ? sessionForEntity(e) : nullptr;
        // A bot has no session, so the roster is what tells a bot's body apart
        // from a flower whose connection simply went away.
        Bot* bot = isPlayer && session == nullptr ? botForEntity(e) : nullptr;

        // A player's corpse KEEPS its Dead tag, which is what puts the dead
        // face and the dead state on the wire and what makes every system step
        // over the body. That means this loop meets the same corpse on every
        // later tick, so everything below has to happen exactly once.
        if (isPlayer) {
            if (auto* pads = world_.tryGet<TeleporterState>(e)) pads->markHoldUntilMillis = 0;
            if (session == nullptr && bot == nullptr) {
                // Nobody is watching through this body and nothing owns it. It
                // leaves no corpse and no death notice, and its ring goes with
                // it: a petal outliving its owner orbits a point in space
                // forever. So do its pets: a splitter half killed in combat is
                // reaped on the tick it died, AFTER the ring pass, so the pass
                // that recalls a downed flower's summons never sees it down.
                destroyBody(e);
                continue;
            }
            if (session != nullptr ? session->deathReported : bot->deathAnnounced) continue;
        }

        // Ankh is a break-triggered relocation, not a revive. Resolve it before
        // the reaper deletes the petal's last position and owner link.
        if (const auto* petal = world_.tryGet<PetalInstance>(e);
            petal && petal->configIndex < content().petalCount() &&
            content().petal(petal->configIndex).id == "ankh") {
            const auto* at = world_.tryGet<Transform>(e);
            auto* ownerAt = world_.tryGet<Transform>(petal->owner);
            const auto* health = world_.tryGet<Health>(petal->owner);
            const auto* body = world_.tryGet<Body>(petal->owner);
            if (at && ownerAt && health && health->alive() && !world_.has<Dead>(petal->owner) &&
                at->realm == ownerAt->realm) {
                ownerAt->position = terrain_->resolveCircle(at->position, body ? body->radius : kPlayerBaseRadius,
                                                            ownerAt->realm);
                if (auto* motion = world_.tryGet<Motion>(petal->owner)) motion->velocity = {};
            }
        }

        if (world_.has<NetId>(e)) {
            const Transform* transform = world_.tryGet<Transform>(e);
            events_.killed(world_.get<NetId>(e).value, transform ? transform->position : Vec2{},
                           transform ? transform->realm : Realm::Overworld);
        }

        if (isPlayer) {
            if (session != nullptr) {
                session->deathReported = true;
                const Entity killer = CombatSystem::creditedPlayer(world_, world_.get<Dead>(e).killer);
                const auto fromHyperCorruption = [&](Entity player) {
                    const auto* visuals = world_.tryGet<PlayerVisuals>(player);
                    return visuals && (visuals->hyperCorruption || visuals->nearbyCorruption ||
                                       visuals->hyperCorruptionExposed);
                };
                session->respawnAtDoor = killer != NULL_ENTITY && killer != e &&
                    (fromHyperCorruption(killer) || fromHyperCorruption(e));
                if (const auto* at = world_.tryGet<Transform>(e)) {
                    session->hasDeathSite = true;
                    session->deathRealm = at->realm;
                    session->deathPosition = at->position;
                }
                // In the ring a death hands the run over to the killer before
                // anything is written down.
                if (session->arena) settleArenaDeath(*session, world_.get<Dead>(e).killer);
                offerSacrifice(*session, e);
                persistPlayer(*session);
                if (net::Connection* connection = listener_.find(session->connection)) {
                    ByteWriter w;
                    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Died));
                    w.str(killerLabel(world_, world_.get<Dead>(e).killer));
                    w.u32(0);
                    w.u32(tick_);
                    w.u32(static_cast<std::uint32_t>(session->runLoot.size()));
                    for (const auto& [item, count] : session->runLoot) {
                        w.u16(item.first);
                        w.u8(static_cast<std::uint8_t>(item.second));
                        w.u32(count);
                    }
                    connection->send(w);
                }
            } else {
                // A bot leaves a corpse too, which is not tidiness: bots carry
                // yggdrasil for each other and actively path to each other's
                // bodies, and a body destroyed on the tick it died is one
                // nothing can ever revive. maintainBots owns the replacement
                // and takes this body away when it builds the new one.
                bot->deathAnnounced = true;
            }
            if (Health* health = world_.tryGet<Health>(e)) {
                health->current = 0;
            }
            // A corpse lies where it fell, at whatever angle it fell at. The
            // roll is the server's so every client sees the same body.
            if (Transform* transform = world_.tryGet<Transform>(e)) {
                transform->angle = rng_.angle();
            }
            continue;
        }
        commands_.destroy(e);
    }
}

void GameServer::replicate(double nowMillis, double wallMillis) {
    Replicator::Frame frame;
    frame.tick = tick_;
    frame.snapshotIndex = ++snapshotIndex_;
    frame.nowMillis = nowMillis;
    frame.wallMillis = wallMillis;
    frame.events = &events_;
    frame.loadShedding = config_.loadShedding;

    deferredPickupViews_.clear();
    std::size_t count = 0;
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.playing()) continue;
        net::Connection* connection = listener_.find(session.connection);
        if (!connection) continue;
        // Let reliable messages and earlier snapshots drain before building
        // another delta. ClientView still describes the last queued snapshot
        // so the next one can carry all state changes since then.
        if (connection->snapshotBacklog() >= kWorldSnapshotBacklogLimit) {
            if (viewpointOf(session) == session.entity)
                deferredPickupViews_.emplace(session.connection, &views_.at(session.connection));
            continue;
        }

        if (count == snapshotJobs_.size()) snapshotJobs_.emplace_back();
        auto& job = snapshotJobs_[count++];
        job.connection = session.connection;
        job.view = &views_.at(session.connection);
        collectSquadBodies(session, job.squad);

        // Built around the flower this client is WATCHING, which is another
        // player's while an admin steers it -- and culled to the window this
        // client reported, which is its own body's claim, not that flower's:
        // the steered player's own stream keeps the box their screen asked
        // for. Nothing here decides whether the control still holds;
        // serviceControl did, this tick, and every way out of it since has
        // ended it on the spot.
        const Entity viewpoint = viewpointOf(session);
        frame.splitOther = viewpoint == session.entity ? session.splitOther : NULL_ENTITY;
        frame.wave = dungeons_ ? dungeons_->waveHudFor(world_, viewpoint, frame.nowMillis) : net::WaveHud{};
        frame.viewport.reset();
        if (viewpoint != session.entity) {
            if (const PlayerLocation* own = world_.tryGet<PlayerLocation>(session.entity)) {
                frame.viewport = own->viewport;
            }
        }

        job.viewer = viewpoint;
        frame.visibility = session.snapshotVisibility;
        job.frame = frame;
    }
    snapshotJobs_.resize(count);
    if (!deferredPickupViews_.empty()) {
        for (const auto& event : events_.events()) {
            if (event.kind != net::EventKind::PickedUp) continue;
            const auto found = deferredPickupViews_.find(event.dealtByConnection);
            if (found != deferredPickupViews_.end()) found->second->deferPickup(event);
        }
    }
    // Build one spatial view of the final replicated positions. A single
    // viewer can walk the world directly. Several viewers reuse the index.
    if (count > 1) {
        snapshotGrid_.clear();
        bool complete = true;
        Query<NetId, Replicated, Transform>{world_}.each(
            [&](Entity e, NetId&, Replicated&, Transform& at) {
                if (!std::isfinite(at.position.x) || !std::isfinite(at.position.y)) {
                    complete = false;
                    return;
                }
                snapshotGrid_.insert(e, at.realm, at.position);
            });
        if (complete)
            for (auto& job : snapshotJobs_) job.frame.entityGrid = &snapshotGrid_;
    }
    // No ECS, session, event or socket mutation until all readers finish.
    parallelSnapshots_->build(world_, snapshotJobs_);
    for (auto& job : snapshotJobs_) {
        if (auto* connection = listener_.find(job.connection))
            if (!job.output.empty()) connection->send(job.output);
    }
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

Session* GameServer::sessionFor(net::ConnectionId id) {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

Session* GameServer::sessionForEntity(Entity e) {
    for (auto& entry : sessions_) {
        // EITHER half. A splitter's parked body is still this person's -- a
        // drop it walks over, a kill its ring lands and a pad it is standing
        // on all belong to the same account -- and a lookup that only knew
        // about the steered one would credit them to nobody.
        if (entry.second.owns(e)) return &entry.second;
    }
    return nullptr;
}

void GameServer::onConnect(net::Connection& connection) {
    const auto durableMessage=[](const std::byte* bytes,std::size_t size) {
        if(!size)return false;
        const auto kind=static_cast<net::ServerMessage>(std::to_integer<std::uint8_t>(bytes[0]));
        return kind!=net::ServerMessage::Snapshot&&kind!=net::ServerMessage::Pong;
    };
    if(config_.authorityDeltaCommit)
        connection.setDurableGate([this](bool durable,std::uint64_t& ticket){return drainAuthoritative(durable,ticket);},durableMessage);
    else if (database_.authoritative())
        connection.setWriteGate([this](bool durable) {
            if (database_.commitUncertain()) return commitBeforeReply();
            return !persistenceFrozen_ && (!durable || commitBeforeReply());
        },durableMessage);
    Session session;
    session.connection = connection.id();
    session.clientAddress = clientAddress(connection.peer(), connection.forwarded());
    sessions_[connection.id()] = std::move(session);
    views_[connection.id()] = ClientView{};
}

void GameServer::onDisconnect(net::Connection& connection, const std::string&) {
    endSession(connection.id());
}

void GameServer::endSession(net::ConnectionId id) {
    authorityAuthQueue_.erase(id);
    if(authorityAuthWork_.erase(id)&&config_.authorityAuth) {
        Json finish=Json::object();finish["action"]="finishAuth";finish["attempt"]=static_cast<double>(id);config_.authorityAuth(finish);
    }
    if (Session* session = sessionFor(id)) {
        // Before the body is destroyed, so the line the squad is told still
        // knows what this flower was called.
        departSquad(*session, nullptr, squadDisplayName(squadIdOf(*session)));
        // Saved only while Playing (persistPlayer's own rule), but taken out
        // whenever a body is held at all: the session is erased below, and a
        // body it still referred to would stand in the world for nobody,
        // streamed to everyone near it, until something killed it. The stage
        // and the body agree on every path there is; this is what keeps one
        // that some day does not from leaking a flower per connection.
        if (session->playing()) persistPlayer(*session);
        if (session->entity != NULL_ENTITY) despawnPlayer(*session, false);
    }
    revokeTempAdmin(id);
    sessions_.erase(id);
    views_.erase(id);
}

void GameServer::reapSessionsWithoutSockets() {
    // Collected first: endSession erases from sessions_, and it tells the
    // squad, which reads other sessions, so nothing may be ended mid-walk.
    std::vector<net::ConnectionId> orphaned;
    for (const auto& entry : sessions_) {
        if (listener_.find(entry.first) == nullptr) orphaned.push_back(entry.first);
    }
    for (const net::ConnectionId id : orphaned) {
        const Session& session = sessions_.at(id);
        std::printf("[SESSION] connection %u closed without a disconnect; ending its session (%s%s)\n",
                    static_cast<unsigned>(id),
                    session.username.empty() ? "not signed in" : session.username.c_str(),
                    session.entity != NULL_ENTITY ? ", in the world" : "");
        std::fflush(stdout);
        endSession(id);
    }
}

bool GameServer::prepareAuthorityAuth(Session& session,net::Connection& connection,net::ClientMessage kind,ByteReader reader) {
    auto& work=authorityAuthWork_.at(connection.id());
    const auto refuse=[&](net::AuthStatus status,const std::string& message){
        work.done=true;sendAuthResult(connection,status,"","",message);return false;
    };
    if(!work.charged) {
        work.charged=true;
        if(!spend(session.loginAttemptsAllowed))return refuse(net::AuthStatus::RateLimited,"Too many attempts. Wait a moment.");
        if(kind==net::ClientMessage::Login) {
            const auto verdict=accountLimits_.spendLoginAttempt(addressKey(session.clientAddress),monotonicMillis());
            if(!verdict.allowed)return refuse(net::AuthStatus::RateLimited,verdict.message);
        }
    }
    if(kind==net::ClientMessage::RegisterGuest)return true;
    const std::string nameOrToken=reader.str();
    const std::string password=kind==net::ClientMessage::ResumeSession?std::string():reader.str();
    if(!reader.ok())return refuse(net::AuthStatus::ServerError,"Invalid authentication request.");
    const bool resume=kind==net::ClientMessage::ResumeSession,registration=kind==net::ClientMessage::Register;
    const auto tokenHash=resume?crypto::sha256Hex(nameOrToken):std::string();
    if(!work.lookedUp) {
        Json query=Json::object();query["action"]="lookup";query["attempt"]=static_cast<double>(connection.id());
        if(resume)query["sessionHash"]=tokenHash;else query["username"]=nameOrToken;
        const auto result=config_.authorityAuth(query);if(result["pending"].asBool())return false;
        if(!result["ok"].asBool())return refuse(net::AuthStatus::ServerError,"Account service temporarily unavailable.");
        work.lookup=result;work.lookedUp=true;
    }
    const auto& identity=work.lookup["identity"];const auto key=work.lookup["identityKey"].asString();
    const auto account=identity["id"].asString();
    if(!work.lookup["found"].asBool()) {
        if(registration)return true;
        return refuse(resume?net::AuthStatus::SessionExpired:net::AuthStatus::BadCredentials,
                      resume?"Please log in again.":"Invalid username or password");
    }
    if(key.empty()||account.empty()||!identity.isObject())return refuse(net::AuthStatus::ServerError,"Account service returned invalid identity.");
    if(!registration&&!work.proven) {
        Json root=Json::object();root["users"][key]=identity;root["players"]=Json::object();
        if(resume)root["sessions"][tokenHash]=work.lookup["session"];
        Database proof;std::string error;
        if(!proof.loadAuthoritative(root.dump(),[](const std::string&){return Database::CommitOutcome::Rejected;},error))
            return refuse(net::AuthStatus::ServerError,"Account proof could not be read.");
        if(resume?!proof.resolveSession(nameOrToken,false):!proof.passwordMatches(nameOrToken,password))
            return refuse(resume?net::AuthStatus::SessionExpired:net::AuthStatus::BadCredentials,
                          resume?"Please log in again.":"Invalid username or password");
        work.proven=true;
    }
    if(!registration&&!work.acquired) {
        Json query=Json::object();query["action"]="acquireAsync";query["attempt"]=static_cast<double>(connection.id());
        query["account"]=account;query["identityKey"]=key;query["expectedIdentity"]=identity;
        if(resume){query["sessionHash"]=tokenHash;query["expectedSession"]=work.lookup["session"];}
        const auto result=config_.authorityAuth(query);if(result["pending"].asBool())return false;
        if(!result["ok"].asBool()) {
            const auto error=result["error"].asString();
            if(error=="transport_unknown"||error=="commit_unknown"||error=="authority_frozen"||error=="commit_outcome_unknown") {
                persistenceFrozen_=true;running_.store(false);
            }
            return refuse(net::AuthStatus::ServerError,"Account is active on another server or temporarily unavailable.");
        }
        work.grant=result;work.acquired=true;
    }
    const bool alreadyOwned=!registration&&work.grant["alreadyOwned"].asBool();
    if(alreadyOwned) {
        const auto* local=std::as_const(database_).findUserById(account);
        if(!local||accountRecordJson(*local).dump()!=accountRecordJson(parseAccountRecord(identity,key)).dump())
            return refuse(net::AuthStatus::ServerError,"Account changed during sign-in. Please try again.");
        if(resume&&!database_.resolveSession(nameOrToken,false))
            return refuse(net::AuthStatus::SessionExpired,"Please log in again.");
    }
    Json delta=Json::object();
    if(!alreadyOwned)delta["users"][key]=identity;
    if(resume&&!alreadyOwned)delta["sessions"][tokenHash]=work.lookup["session"];
    if(!registration&&!alreadyOwned)delta["players"][account]=work.grant["record"];
    if(!database_.applyAuthoritativeDelta(delta,true)) {
        if(!database_.save()&&!database_.commitPending())return refuse(net::AuthStatus::ServerError,"Account save is unavailable.");
        return false;
    }
    if(!registration&&std::find(authorityLiveAccounts_.begin(),authorityLiveAccounts_.end(),account)==authorityLiveAccounts_.end())
        authorityLiveAccounts_.push_back(account);
    return true;
}

void GameServer::onMessage(net::Connection& connection, ByteReader& reader) {
    if (persistenceFrozen_) return;
    ByteReader original=reader;
    Session* session = sessionFor(connection.id());
    if (!session) return;

    const auto id = static_cast<net::ClientMessage>(reader.u8());
    struct MeasureArrivalMessage {
        ArrivalProbe* probe;
        net::ClientMessage id;
        double start;
        ~MeasureArrivalMessage() {
            if (!probe) return;
            const double elapsed = monotonicMillis() - start;
            ++probe->messages;
            probe->messageMillis += elapsed;
            auto& message = probe->byType[static_cast<unsigned>(id)];
            ++message.count;
            message.millis += elapsed;
            if (elapsed <= probe->slowestMillis) return;
            probe->slowestMillis = elapsed;
            probe->slowestMessage = static_cast<unsigned>(id);
        }
    } measure{arrivalProbe_, id, arrivalProbe_ ? monotonicMillis() : 0.0};
    if (session->stage == SessionStage::Greeting && id != net::ClientMessage::Hello) {
        connection.closeGracefully();return;
    }
    const bool authentication=id==net::ClientMessage::Login||id==net::ClientMessage::ResumeSession||
        id==net::ClientMessage::Register||id==net::ClientMessage::RegisterGuest;
    if(config_.authorityAuth&&authentication) {
        if(processingAuthorityAuth_!=connection.id()) {
            if(authorityAuthWork_.count(connection.id())||original.remaining()>8192||authorityAuthWork_.size()>=256)return;
            auto& work=authorityAuthWork_[connection.id()];
            while(original.remaining())work.packet.push_back(static_cast<std::byte>(original.u8()));
            return;
        }
        if(!prepareAuthorityAuth(*session,connection,id,reader))return;
        authorityAuthWork_.at(connection.id()).done=true;
    }
    if(config_.authorityRefresh&&(id==net::ClientMessage::Login||id==net::ClientMessage::ResumeSession||
        id==net::ClientMessage::Register||id==net::ClientMessage::RegisterGuest)) {
        if(authorityAuthQueue_.count(connection.id()))return;
        if(!refreshAuthority()) {
            if(!persistenceFrozen_&&original.remaining()<=8192) {
                auto& queued=authorityAuthQueue_[connection.id()];
                while(original.remaining())queued.push_back(static_cast<std::byte>(original.u8()));
            }
            return;
        }
    }

    // Nothing but the handshake is accepted until the protocol has been agreed.
    // A client that skips it cannot reach any game logic at all.
    if (session->stage == SessionStage::Greeting && id != net::ClientMessage::Hello) {
        connection.closeGracefully();
        return;
    }

    // Bill before dispatch, including invalid requests. These handlers can
    // reapply a whole ring, scan the bag and send a full Profile; without a
    // shared ceiling a modified client can do that once per packet, and
    // alternate message types to evade a per-handler limit. Excess requests
    // have no side effects and produce no reply to amplify the flood.
    switch (id) {
        case net::ClientMessage::SetLoadout:
        case net::ClientMessage::SwapLoadout:
        case net::ClientMessage::SwapLoadoutRows:
        case net::ClientMessage::SaveLoadoutPreset:
        case net::ClientMessage::LoadLoadoutPreset:
        case net::ClientMessage::BulkCraft:
        case net::ClientMessage::DailyDropBonus:
        case net::ClientMessage::RequestWaveRoomStatus:
        case net::ClientMessage::DailyLoginClaim:
        case net::ClientMessage::AccountQQ:
        case net::ClientMessage::SkinReview:
        case net::ClientMessage::Craft:
        case net::ClientMessage::OracleCraft:
        case net::ClientMessage::Trade:
        case net::ClientMessage::TitanForge:
        case net::ClientMessage::Assemble:
        case net::ClientMessage::Absorb:
        case net::ClientMessage::BuyPetal:
        case net::ClientMessage::UpgradeSkill:
        case net::ClientMessage::DowngradeSkill:
        case net::ClientMessage::ResetSkills:
        case net::ClientMessage::RedeemCode:
            if (!session->authenticated() || !spend(session->inventoryAllowance)) return;
            break;
        default: break;
    }

    switch (id) {
        case net::ClientMessage::Hello:         handleHello(*session, connection, reader); break;
        case net::ClientMessage::Register:      handleRegister(*session, connection, reader); break;
        case net::ClientMessage::RegisterGuest: handleRegisterGuest(*session, connection); break;
        case net::ClientMessage::SetUsername:   handleSetUsername(*session, connection, reader); break;
        case net::ClientMessage::Login:         handleLogin(*session, connection, reader); break;
        case net::ClientMessage::ResumeSession: handleResume(*session, connection, reader); break;
        case net::ClientMessage::ChangePassword:
            handleChangePassword(*session, connection, reader);
            break;
        case net::ClientMessage::JoinGame:      handleJoin(*session, connection, reader); break;
        case net::ClientMessage::LeaveGame:     handleLeave(*session, connection); break;
        case net::ClientMessage::Input:         handleInput(*session, connection, reader); break;
        case net::ClientMessage::Chat:          handleChat(*session, connection, reader); break;
        case net::ClientMessage::SetLoadout:    handleSetLoadout(*session, reader); break;
        case net::ClientMessage::BulkCraft: handleBulkCraft(*session, connection, reader); break;
        case net::ClientMessage::DailyDropBonus: handleDropBonus(*session, connection, reader); break;
        case net::ClientMessage::RequestWaveRoomStatus: {
            ByteWriter status;
            status.u8(static_cast<std::uint8_t>(net::ServerMessage::WaveRoomStatus));
            status.u8(dungeons_ ? dungeons_->waveOccupancy() : 0);
            status.boolean(database_.waveDailyStarCapReached(session->userId));
            connection.send(status);
            break;
        }
        case net::ClientMessage::AccountQQ: {
            const bool save = reader.boolean();
            const std::string value = save ? reader.str() : std::string();
            if (!reader.ok() || !session->authenticated() || value.size() > 100) break;
            auto& record = liveRecord(*session);
            if (save) {
                record.extra["qq"] = value;
                database_.markDirty();
            }
            ByteWriter reply;
            reply.u8(static_cast<std::uint8_t>(net::ServerMessage::AccountQQ));
            reply.boolean(save);
            reply.str(record.extra["qq"].asString());
            connection.send(reply);
            break;
        }
        case net::ClientMessage::DailyLoginClaim:
            if (session->authenticated()) {
                sendDailyStreak(*session, connection);
                sendProfile(*session, connection);
            }
            break;
        case net::ClientMessage::ProfileCheck:  handleProfileCheck(*session, connection, reader); break;
        case net::ClientMessage::SwapLoadout:   handleSwapLoadout(*session, reader); break;
        case net::ClientMessage::SwapLoadoutRows: handleSwapLoadoutRows(*session, connection); break;
        case net::ClientMessage::SaveLoadoutPreset:
            handleSaveLoadoutPreset(*session, connection, reader);
            break;
        case net::ClientMessage::LoadLoadoutPreset:
            handleLoadLoadoutPreset(*session, connection, reader);
            break;
        case net::ClientMessage::UsePetal:      handleUsePetal(*session, reader); break;
        case net::ClientMessage::Craft:         handleCraft(*session, connection, reader); break;
        case net::ClientMessage::OracleCraft:   handleOracleCraft(*session, connection, reader); break;
        case net::ClientMessage::Trade:         handleTrade(*session, connection, reader); break;
        case net::ClientMessage::TitanForge:    handleTitanForge(*session, connection, reader); break;
        case net::ClientMessage::Assemble:      handleAssemble(*session, connection, reader); break;
        case net::ClientMessage::Absorb:        handleAbsorb(*session, connection, reader); break;
        case net::ClientMessage::TitanHolder:   handleTitanHolder(*session, connection, reader); break;
        case net::ClientMessage::Respawn:       handleRespawn(*session); break;
        case net::ClientMessage::EnterDungeon:
            if(session->playing()&&dungeons_&&spend(session->inventoryAllowance))dungeons_->requestEntry(session->entity,clockMillis_);break;
        case net::ClientMessage::Ping:          handlePing(connection, reader); break;
        case net::ClientMessage::UpgradeSkill:  handleUpgradeSkill(*session, connection, reader); break;
        case net::ClientMessage::DowngradeSkill:
            handleDowngradeSkill(*session, connection, reader);
            break;
        case net::ClientMessage::ResetSkills:   handleResetSkills(*session, connection); break;
        case net::ClientMessage::BuyPetal:      handleBuyPetal(*session, connection, reader); break;
        case net::ClientMessage::RedeemCode:    handleRedeemCode(*session, connection, reader); break;
        case net::ClientMessage::SetSkin:       handleSetSkin(*session, connection, reader); break;
        case net::ClientMessage::RequestPracticeBoard:
            if (session->authenticated() && spend(session->inventoryAllowance)) {
                handlePracticeBoard(*session, connection, reader);
            }
            break;
        case net::ClientMessage::RequestLeaderboard:
            handleLeaderboard(*session, connection, reader);
            break;
        case net::ClientMessage::RequestNotifications: handleNotifications(connection, reader); break;
        case net::ClientMessage::GuildCreate:   handleGuildCreate(*session, connection, reader); break;
        case net::ClientMessage::GuildEdit:     handleGuildEdit(*session, connection, reader); break;
        case net::ClientMessage::GuildInvite:   handleGuildInvite(*session, connection, reader); break;
        case net::ClientMessage::GuildAccept:   handleGuildAccept(*session, connection); break;
        case net::ClientMessage::GuildDecline:  handleGuildDecline(*session, connection); break;
        case net::ClientMessage::GuildKick:     handleGuildKick(*session, connection, reader); break;
        case net::ClientMessage::GuildLeave:    handleGuildLeave(*session, connection); break;
        case net::ClientMessage::GuildSquadAll: handleGuildSquadAll(*session, connection); break;
        case net::ClientMessage::GuildInviteToSquad:
            handleGuildInviteToSquad(*session, connection, reader);
            break;
        case net::ClientMessage::PublishSkin:   handlePublishSkin(*session, connection, reader); break;
        case net::ClientMessage::EquipSkin:     handleEquipSkin(*session, connection, reader); break;
        case net::ClientMessage::DeleteSkin:    handleDeleteSkin(*session, connection, reader); break;
        case net::ClientMessage::SkinReview:     handleSkinReview(*session, connection, reader); break;
        case net::ClientMessage::Logout:        handleLogout(*session); break;
        case net::ClientMessage::AdminDb:       handleAdminDb(*session, connection, reader); break;
        case net::ClientMessage::AdminDashboard:
            handleAdminDashboard(*session, connection, reader);
            break;
        default:
            break;
    }
}

void GameServer::handleHello(Session& session, net::Connection& connection, ByteReader& reader) {
    // Once per socket. The shipping client sends its Hello from
    // NetClient::onConnect, which the transport calls once per dial, so a
    // second one on a live socket is a modified client -- and it used to be
    // honoured. The stage went back to Anonymous with the body, the account
    // and any temporary grant all still on the session, and everything that
    // looks after a body asks playing(): the periodic save, the disconnect,
    // leaveWorld and the next join. So the flower stood in the world for
    // nobody, unsaved and walking its last input, a steered one included, and
    // every (Hello, Resume, Join) left another -- without limit, for one free
    // account, until a restart. A repeat is closed instead, and the stage it
    // arrived at is left alone: the close then runs through onDisconnect like
    // any other, which saves the body and takes it out. Were a re-handshake
    // ever wanted, it would have to be a signOut(), never a bare stage write.
    if (session.stage != SessionStage::Greeting) {
        connection.closeGracefully();
        return;
    }

    const std::uint16_t version = reader.u16();
    const std::uint32_t clientContent = reader.u32();
    if (!reader.ok()) { connection.closeGracefully(); return; }

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Welcome));
    w.u16(net::kProtocolVersion);

    if (version != net::kProtocolVersion) {
        w.boolean(false);
        w.str("This client speaks protocol " + std::to_string(version) + "; the server speaks " +
              std::to_string(net::kProtocolVersion) + ". Please update.");
        connection.send(w);
        connection.closeGracefully();
        return;
    }
    if (clientContent != content().contentHash()) {
        // Mob and petal stats are read from JSON by both sides. If the two read
        // different files, every number the client shows is quietly wrong --
        // far better to say so at connect time.
        w.boolean(false);
        w.str("Your game content does not match the server's. Please update.");
        connection.send(w);
        connection.closeGracefully();
        return;
    }

    w.boolean(true);
    w.str("");
    connection.send(w);
    session.stage = SessionStage::Anonymous;
}

void GameServer::sendAuthResult(net::Connection& connection, net::AuthStatus status,
                                const std::string& token, const std::string& username,
                                const std::string& reason) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::AuthResult));
    w.u8(static_cast<std::uint8_t>(status));
    w.str(token);
    w.str(username);
    w.str(reason);
    connection.send(w);
}

namespace {

/// The wire's answer for each way createUser refuses. Every refusal used to go
/// out as UsernameTaken, which the client reads as "that account exists, log
/// in instead" -- so a name the format rules refused sent an auto-login rig
/// off to sign in to an account that was never made.
net::AuthStatus refusalStatus(CreateStatus status) {
    switch (status) {
        case CreateStatus::UsernameInvalid: return net::AuthStatus::UsernameInvalid;
        case CreateStatus::UsernameTaken:   return net::AuthStatus::UsernameTaken;
        case CreateStatus::PasswordInvalid: return net::AuthStatus::PasswordInvalid;
        // "Made" with no account to show for it is no account either.
        case CreateStatus::Ok:
        case CreateStatus::ServerError:     break;
    }
    return net::AuthStatus::ServerError;
}

} // namespace

void GameServer::handleRegister(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::string username = reader.str();
    const std::string password = reader.str();
    if (!reader.ok()) return;

    if (!config_.authorityAuth&&!spend(session.loginAttemptsAllowed)) {
        sendAuthResult(connection, net::AuthStatus::RateLimited, "", "", "Too many attempts. Wait a moment.");
        return;
    }

    // The database's own rules -- the one copy, which createUser applies again
    // below -- so this answer and that one cannot disagree.
    std::string reason;
    if (!Database::validUsername(username, reason)) {
        sendAuthResult(connection, net::AuthStatus::UsernameInvalid, "", "", reason);
        return;
    }
    if (!Database::validPassword(password, reason)) {
        sendAuthResult(connection, net::AuthStatus::PasswordInvalid, "", "", reason);
        return;
    }

    // Below the format checks, so mistyping a password does not cost one of the
    // three accounts an address gets: those checks are pure string work and are
    // already bounded by the per-session budget above. Above the bcrypt hash and
    // the database write, which are the work an unlimited registration endpoint
    // hands an attacker.
    const std::string addressHash = database_.accountAddressHash(addressKey(session.clientAddress));
    if (!spendRegistration(session, connection, addressHash)) return;

    const CreateResult result = database_.createUser(username, password, addressHash);
    if (!result.ok() || !result.account) {
        sendAuthResult(connection, refusalStatus(result.status), "", "", result.reason);
        return;
    }
    beginNewAccount(session, connection, *result.account);
}

void GameServer::handleRegisterGuest(Session& session, net::Connection& connection) {
    // A guest IS an account -- a row in the file, a starter kit, a name on the
    // leaderboard -- so it is held to everything Register is, the per-socket
    // budget and the address limits both. Making guests cheaper than accounts
    // would only move the nine thousand spam accounts to this door.
    if (!config_.authorityAuth&&!spend(session.loginAttemptsAllowed)) {
        sendAuthResult(connection, net::AuthStatus::RateLimited, "", "",
                       "Too many attempts. Wait a moment.");
        return;
    }
    const std::string addressHash = database_.accountAddressHash(addressKey(session.clientAddress));
    if (!spendRegistration(session, connection, addressHash)) return;

    const CreateResult result = database_.createGuest(addressHash);
    if (!result.ok() || !result.account) {
        sendAuthResult(connection, net::AuthStatus::ServerError, "", "",
                       result.reason.empty() ? "Could not make a guest account." : result.reason);
        return;
    }
    beginNewAccount(session, connection, *result.account);
}

bool GameServer::spendRegistration(const Session& session, net::Connection& connection,
                                   const std::string& addressHash) {
    // The session budget each caller spends first bounds one SOCKET; it does
    // not bound one client, which can hang up and dial again for a fresh
    // allowance. These limits are keyed on the address instead, and outlive
    // any one connection -- and the address's standing, which sizes them, is
    // read from the accounts it made, so they outlive a restart too.
    const std::string address = addressKey(session.clientAddress);
    const LimitVerdict verdict = accountLimits_.spendRegistration(
        address,
        [&] {
            const Database::CreatorStanding made =
                database_.creatorStanding(addressHash, kIdleWindowMillis, kStandingWindowMillis);
            return AddressStanding{made.idleToday, made.played};
        },
        monotonicMillis());
    if (verdict.allowed) return true;
    // The moment an address goes to the penalty box is always worth a line;
    // the refusals around it are throttled, so a flood is not also a log flood.
    if (verdict.newlyBlocked) {
        std::printf("[ABUSE] %s kept asking after being refused; no new accounts from it for "
                    "%d minutes\n",
                    address.c_str(), static_cast<int>(kBlockMillis / 60000));
    }
    const std::string line = accountLimits_.refusalLogLine(address, verdict.scope, monotonicMillis());
    if (!line.empty()) std::printf("%s\n", line.c_str());
    sendAuthResult(connection, net::AuthStatus::RateLimited, "", "", verdict.message);
    return false;
}

void GameServer::beginNewAccount(Session& session, net::Connection& connection,
                                 const Account& account) {
    // A socket still in the world leaves it first, its body saved into the
    // account it belonged to (see leaveWorld).
    leaveWorld(session);
    releaseOnAccountChange(session, account.id);
    session.userId = account.id;
    session.username = account.username;
    // Assigned, never left as it was: an account can be made on a socket that
    // is already signed in, and a full admin's flag must not carry over into
    // the account it has just made. A new account is never an admin.
    session.admin = account.adminActive();
    session.adminTier = account.adminTier;
    session.token = database_.createSession(session.userId, session.username);

    grantStarterKit(database_.progress(session.userId), account.id);
    database_.markDirty();
    session.stage = SessionStage::Authenticated;
    sendAuthResult(connection, net::AuthStatus::Ok, session.token, session.username, "");
    sendDailyStreak(session, connection);
    sendProfile(session, connection);
    sendSkinCatalog(session, connection);
    sendGuildState(session, connection);
    sendChatHistory(connection);
}

void GameServer::handleLogin(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::string username = reader.str();
    const std::string password = reader.str();
    if (!reader.ok()) return;

    if (!config_.authorityAuth&&!spend(session.loginAttemptsAllowed)) {
        sendAuthResult(connection, net::AuthStatus::RateLimited, "", "", "Too many attempts. Wait a moment.");
        return;
    }
    // Same reasoning as handleRegister: a reconnect resets the session budget,
    // and every attempt costs this server a bcrypt verify.
    const std::string address = addressKey(session.clientAddress);
    const LimitVerdict verdict = config_.authorityAuth?LimitVerdict{}:accountLimits_.spendLoginAttempt(address, monotonicMillis());
    if (!config_.authorityAuth&&!verdict.allowed) {
        sendAuthResult(connection, net::AuthStatus::RateLimited, "", "", verdict.message);
        return;
    }

    // A standalone login can verify and update activity together. The shared
    // path must still check credentials before it tries to acquire ownership.
    const bool standaloneLogin = !config_.authorityAuth && !config_.authorityAcquire &&
                                 !config_.authorityRefresh && !database_.authoritative();
    if (!config_.authorityAuth &&
        !(standaloneLogin ? database_.verifyPassword(username, password)
                          : database_.passwordMatches(username, password))) {
        // Deliberately the same answer for a wrong password and an unknown
        // account: distinguishing them tells an attacker which names exist.
        sendAuthResult(connection, net::AuthStatus::BadCredentials, "", "",
                       "Invalid username or password");
        return;
    }

    const Account* account = database_.findUser(username);
    if (!account) {
        sendAuthResult(connection, net::AuthStatus::ServerError, "", "", "Account could not be read.");
        return;
    }
    // A player who signed in is not what the limit is for.
    if(!acquireAuthority(account->id)) {
        sendAuthResult(connection,net::AuthStatus::ServerError,"","","Account is active on another server or temporarily unavailable.");
        return;
    }
    if (!standaloneLogin && !database_.verifyPassword(username, password)) return;
    accountLimits_.refundLoginAttempt(address);

    // A socket still in the world leaves it first, its body saved into the
    // account it belonged to -- this one again, or another (see leaveWorld).
    leaveWorld(session);
    releaseOnAccountChange(session, account->id);
    replaceOtherSessions(session, account->id);
    session.userId = account->id;
    session.username = account->username;
    session.admin = account->adminActive();
    session.adminTier = account->adminTier;
    session.token = database_.createSession(account->id, account->username);
    session.stage = SessionStage::Authenticated;
    sendAuthResult(connection, net::AuthStatus::Ok, session.token, account->username, "");
    sendDailyStreak(session, connection);
    sendProfile(session, connection);
    sendSkinCatalog(session, connection);
    sendGuildState(session, connection);
    sendChatHistory(connection);
}

void GameServer::handleResume(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::string token = reader.str();
    if (!reader.ok()) return;

    const Database::Session* record = database_.resolveSession(token,false);
    if (!record) {
        sendAuthResult(connection, net::AuthStatus::SessionExpired, "", "", "Please log in again.");
        return;
    }

    // Copied out before the other sessions are signed out: that saves their
    // bodies into the database, and `record` points into it.
    const std::string userId = record->userId;
    const std::string username = record->username;
    if(!acquireAuthority(userId,crypto::sha256Hex(token))) {
        sendAuthResult(connection,net::AuthStatus::ServerError,"","","Account is active on another server or temporarily unavailable.");
        return;
    }
    if(!database_.resolveSession(token))return;
    // A socket still in the world leaves it first, as a login's does (see
    // leaveWorld).
    leaveWorld(session);
    releaseOnAccountChange(session, userId);
    replaceOtherSessions(session, userId);
    session.userId = userId;
    session.username = username;
    session.token = token;
    session.stage = SessionStage::Authenticated;
    // Assigned whether or not the lookup lands, so a flag left on this socket
    // by whoever was signed in on it before can never ride along.
    Account* account = database_.findUser(username);
    session.admin = account != nullptr && account->adminActive();
    session.adminTier = account != nullptr ? account->adminTier : kTopAdminTier;
    sendAuthResult(connection, net::AuthStatus::Ok, token, username, "");
    sendDailyStreak(session, connection);
    sendProfile(session, connection);
    sendSkinCatalog(session, connection);
    sendGuildState(session, connection);
    sendChatHistory(connection);
}

void GameServer::sendChangePasswordResult(net::Connection& connection, bool ok,
                                          const std::string& token, const std::string& reason) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::ChangePasswordResult));
    w.boolean(ok);
    w.str(token);
    w.str(reason);
    connection.send(w);
}

void GameServer::handleChangePassword(Session& session, net::Connection& connection,
                                      ByteReader& reader) {
    const std::string current = reader.str();
    const std::string next = reader.str();
    if (!reader.ok()) return;

    // WHOSE password this is comes from the session and from nowhere else. A
    // username on the wire would make this an endpoint for changing another
    // account's password, and an anonymous socket has no account to name.
    if (!session.authenticated() || session.username.empty()) {
        sendChangePasswordResult(connection, false, "", "You are not signed in.");
        return;
    }
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    const bool offload = database_.backgroundSaveEnabled();
    if (offload && passwordChange_.future.valid()) {
        // Keep the pending request as the only reply for this connection.
        if (passwordChange_.connection != connection.id())
            sendChangePasswordResult(connection, false, "", "A password change is in progress. Try again shortly.");
        return;
    }
#endif

    // AshFlor BJ: admins may change their password here (same budgets as anyone).

    // Held to the login budgets, both of them: this costs a bcrypt verify and
    // then a bcrypt hash, so it is strictly dearer than a login, and a wrong
    // `current` is a password guess like any other -- against an account whose
    // socket may have been left open on a shared machine. The per-session
    // budget bounds this connection; the per-address one outlives a reconnect.
    if (!spend(session.loginAttemptsAllowed)) {
        sendChangePasswordResult(connection, false, "", "Too many attempts. Wait a moment.");
        return;
    }
    const std::string address = addressKey(session.clientAddress);
    const LimitVerdict verdict = accountLimits_.spendLoginAttempt(address, monotonicMillis());
    if (!verdict.allowed) {
        sendChangePasswordResult(connection, false, "", verdict.message);
        return;
    }

    // An account whose owner was never told its password -- a guest's random
    // one -- has no current password to produce, so the session is the proof:
    // the token that opened it is the only credential the account has ever
    // had. Every other account proves itself again, because a socket left open
    // on a shared machine is exactly who must not be able to lock its owner out.
    const Account* account = std::as_const(database_).findUserById(session.userId);
    const bool proven = account != nullptr && !account->passwordChosen;
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    if (offload && account) {
        const int cost = database_.passwordCost();
        passwordChange_.connection = connection.id();
        passwordChange_.token = session.token;
        passwordChange_.address = address;
        passwordChange_.expected = *account;
        try {
            passwordChange_.future = std::async(std::launch::async,
                [expected = *account, current, next, cost] {
                    PasswordChangeResult result;
                    const bool first = !expected.passwordChosen;
                    result.verified = first || Database::passwordMatches(expected, current);
                    if (!result.verified) result.reason = "Your current password is not correct.";
                    else if (!first && current == next) result.reason = "That is already your password.";
                    else if (Database::validPassword(next, result.reason)) {
                        if (crypto::secureRandom().seededFromKernel())
                            result.hash = crypto::bcryptHash(next, cost);
                        else
                            result.reason = "Password worker unavailable. Try again shortly.";
                    }
                    return result;
                });
        } catch (const std::system_error&) {
            accountLimits_.refundLoginAttempt(address);
            sendChangePasswordResult(connection, false, "", "Password worker unavailable. Try again shortly.");
        }
        return;
    }
#endif
    if (!proven && !database_.verifyPassword(session.username, current)) {
        sendChangePasswordResult(connection, false, "", "Your current password is not correct.");
        return;
    }
    // Producing the current password is proof of the account, not a guess at
    // it, so it does not spend from the budget -- the same reason a successful
    // login refunds its attempt.
    accountLimits_.refundLoginAttempt(address);

    if (!proven && current == next) {
        // Refused rather than quietly accepted: the sessions below would be
        // revoked and the token reissued for a change that did not happen, so
        // the player would be signed out elsewhere for nothing.
        sendChangePasswordResult(connection, false, "", "That is already your password.");
        return;
    }

    std::string reason;
    if (!database_.setPassword(session.username, next, reason)) {
        sendChangePasswordResult(connection, false, "", reason);
        return;
    }

    // Every token the account had dies with the old password. That is most of
    // what changing one is for: a session somebody else is holding would
    // otherwise outlive it by up to thirty days, and a stolen token is the
    // case a player changes their password over. This connection's own token
    // is among them, so it is replaced in the same breath -- the socket stays
    // authenticated either way, because the stage is in memory rather than in
    // the token, but the client has to be holding a live one to resume with.
    database_.revokeSessionsForUser(session.username);
    session.token = database_.createSession(session.userId, session.username);

    // Rate-limited rather than immediate, and not left to the thirty-second
    // persist either: a player who changes a password and closes the game must
    // not find the old one still working, and a whole-file write per request
    // would be a stall anyone could ask for.
    database_.maybeSave(monotonicMillis());

    sendChangePasswordResult(connection, true, session.token, "");
    // The Account panel draws its form from the Profile's flags, and an account
    // that just chose its first password has a different form now.
    if (proven) sendProfile(session, connection);
}

void GameServer::pollPasswordChange() {
    if (!passwordChange_.future.valid() ||
        passwordChange_.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto work = std::move(passwordChange_);
    passwordChange_ = {};
    PasswordChangeResult result;
    try {
        result = work.future.get();
    } catch (const std::exception&) {
        result.reason = "Password worker unavailable. Try again shortly.";
    }
    if (result.verified) accountLimits_.refundLoginAttempt(work.address);

    auto* peer = listener_.find(work.connection);
    auto* session = sessionFor(work.connection);
    if (!peer || !session || !session->authenticated() || session->token != work.token ||
        session->userId != work.expected.id) return;
    const auto* current = std::as_const(database_).findUserById(work.expected.id);
    if (!current || current->username != work.expected.username ||
        current->passwordHash != work.expected.passwordHash ||
        current->passwordSalt != work.expected.passwordSalt ||
        current->passwordChosen != work.expected.passwordChosen ||
        !database_.resolveSession(work.token, false)) {
        sendChangePasswordResult(*peer, false, "", "Account credentials changed. Sign in again.");
        return;
    }
    if (result.hash.empty()) {
        sendChangePasswordResult(*peer, false, "", result.reason);
        return;
    }
    const bool first = !current->passwordChosen;
    auto& account = *database_.findUserById(work.expected.id);
    std::string token;
    if (!database_.savePasswordChange(account, std::move(result.hash), token)) {
        if (database_.commitUncertain()) {
            persistenceFrozen_ = true;
            running_.store(false);
        } else {
            sendChangePasswordResult(*peer, false, "", "Could not save the new password. Try again shortly.");
        }
        return;
    }
    session->token = std::move(token);
    sendChangePasswordResult(*peer, true, session->token, "");
    if (first) sendProfile(*session, *peer);
}

void GameServer::sendSetUsernameResult(net::Connection& connection, bool ok,
                                       const std::string& username, const std::string& reason) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SetUsernameResult));
    w.boolean(ok);
    w.str(username);
    w.str(reason);
    connection.send(w);
}

void GameServer::handleSetUsername(Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const std::string requested = trimmed(reader.str());
    if (!reader.ok()) return;

    // WHOSE name this is comes from the session, as a password change's does.
    if (!session.authenticated() || session.userId.empty()) {
        sendSetUsernameResult(connection, false, "", "You are not signed in.");
        return;
    }
    // On the login budget: every refusal says whether a name is free, and a
    // free loop of these is a free walk of the user table.
    if (!spend(session.loginAttemptsAllowed)) {
        sendSetUsernameResult(connection, false, "", "Too many attempts. Wait a moment.");
        return;
    }
    // Upstream: guests name themselves once. AshFlor BJ: database admins may
    // rename too (session.admin is the persistent flag, not a temp grant).
    const Account* account = std::as_const(database_).findUserById(session.userId);
    if (account == nullptr || (!account->guest && !session.admin)) {
        sendSetUsernameResult(connection, false, "",
                              "Only a guest or admin account can set its username.");
        return;
    }

    const std::string oldName = account->username;
    std::string reason;
    if (!database_.renameUser(session.userId, requested, reason)) {
        sendSetUsernameResult(connection, false, "", reason);
        return;
    }
    const std::string newName = database_.findUserById(session.userId)->username;
    session.username = newName;
    std::printf("[ACCOUNT] guest %s is now %s\n", oldName.c_str(), newName.c_str());

    // The guild records name their members, and their leader, by account.
    const std::string oldKey = lowerCase(oldName);
    const std::string guildName = guildNameForUser(oldName);
    if (!guildName.empty()) {
        Json& guild = database_.rawTable("guilds")[guildName];
        auto deputies = guildDeputies(guild);
        if (lowerCase(guild["leaderUsername"].asString()) == oldKey) {
            guild["leaderUsername"] = newName;
        }
        for (auto& deputy : deputies)
            if (lowerCase(deputy) == oldKey) deputy = newName;
        setGuildDeputies(guild, deputies);
        Json members = guild["memberUsernames"];
        Json renamed = Json::array();
        for (const Json& member : members.items()) {
            renamed.push(lowerCase(member.asString()) == oldKey ? Json(newName) : member);
        }
        guild["memberUsernames"] = std::move(renamed);
        database_.markDirty();
    }
    // An invitation is addressed to the name it was sent to.
    if (auto invite = guildInvites_.find(oldKey); invite != guildInvites_.end()) {
        guildInvites_[lowerCase(newName)] = invite->second;
        guildInvites_.erase(oldKey);
    }
    // The titan's memo of who holds what is a list of names; one of them may
    // be this one. Forgotten whole -- it is a cache, refilled on the next ask.
    universalHolders_.clear();
    // Soon, not at the next periodic persist: the old name is free from this
    // moment, and a crash that brought it back would hand the player a name
    // somebody else may have taken in the meantime.
    database_.maybeSave(monotonicMillis());

    sendSetUsernameResult(connection, true, newName, "");
    sendProfile(session, connection);
    if (Squad* squad = squads_.forMember(squadIdOf(session))) broadcastSquadUpdate(*squad);
    if (!guildName.empty()) {
        broadcastGuildRoster(std::as_const(database_).storedTable("guilds")[guildName]);
    }
}

void GameServer::handleLogout(Session& session) {
    // An anonymous socket has no account to log out of.
    if (!session.authenticated()) return;
    const std::string userId = session.userId;

    // Every token the account holds, not only this connection's. A logout
    // that left the others alive would leave a copy on a shared machine, or
    // one somebody lifted, good for up to thirty more days -- and "log out"
    // is the button a player reaches for in exactly that situation.
    database_.revokeSessionsForUser(session.username);
    signOut(session);

    // The tokens were only half of it: a connection that already resumed one
    // is authenticated in memory, not by the token, and would play on as the
    // account until it dropped. Each is signed out here and told so with the
    // answer a dead token gets, which is the one its client already knows
    // means "back to the login form".
    for (auto& [id, other] : sessions_) {
        if (id == session.connection || !other.authenticated() || other.userId != userId) continue;
        signOut(other);
        if (net::Connection* connection = listener_.find(id)) {
            sendAuthResult(*connection, net::AuthStatus::SessionExpired, "", "",
                           "This account was logged out elsewhere.");
        }
    }

    // Same reason as a password change: a revocation still waiting for the
    // thirty-second persist is undone by a crash inside that window.
    database_.maybeSave(monotonicMillis());
}

void GameServer::replaceOtherSessions(const Session& incoming, const std::string& userId) {
    // Two live connections on one account are two bodies playing one record:
    // each carries its own XP and stars and the later save wins, and one
    // person farming with several flowers at once is the multiboxing this
    // rule exists to stop. The NEWER sign-in wins, rather than the first
    // being defended: the player at the new tab is the one at the keyboard,
    // and a crashed tab whose socket has not timed out yet must not lock its
    // own owner out.
    //
    // No exemption for loopback. The browser server spared a pair of local
    // tabs, but everything nginx proxies arrives from loopback too, and the
    // peer this transport records carries no forwarding header to tell the
    // two apart -- an exemption here would switch the rule off in production.
    for (auto& [id, other] : sessions_) {
        if (id == incoming.connection || !other.authenticated() || other.userId != userId) continue;
        net::Connection* connection = listener_.find(id);
        std::printf("[SESSION] %s signed in on connection %u; closing connection %u\n",
                    other.username.c_str(), static_cast<unsigned>(incoming.connection),
                    static_cast<unsigned>(id));

        // What a disconnect does, and while the session still has a name: the
        // squad is told who left, and the body is saved into the record BEFORE
        // the incoming connection reads it back, so it starts from where this
        // one stopped rather than from the last periodic save.
        departSquad(other, nullptr, squadDisplayName(squadIdOf(other)));
        // Anonymous from here on, so the close that follows has no account left
        // to persist into -- a late save from this side would overwrite
        // whatever the new session does in the meantime.
        signOut(other);

        if (connection) {
            ByteWriter w;
            w.u8(static_cast<std::uint8_t>(net::ServerMessage::SessionReplaced));
            w.str("You logged in from another tab or device.");
            connection->send(w);
            // Gracefully: the reason has to reach the client before the socket
            // goes, or all it sees is a drop -- which it would redial.
            connection->closeGracefully();
        }
    }
}

void GameServer::signOut(Session& session) {
    // The squad first, while the session still has the name the squad knows
    // it by: a squad is keyed by connection, and an anonymous socket left in
    // one would be handed to whoever signs in on it next, as a disconnect
    // never would. Callers that already departed (replaceOtherSessions, the
    // database editor's sign-out) make this a no-op.
    departSquad(session, listener_.find(session.connection),
                squadDisplayName(squadIdOf(session)));
    // A body in the world belongs to the account that is going away, so it
    // comes off first -- and with its progress saved, because a logout is a
    // deliberate exit, not a drop. Left alone it would be an orphan: a flower
    // nobody can steer and no account can persist. Taking it off is also what
    // ends any flower control this connection is part of, from either end:
    // control needs a body on both sides, and despawnPlayer lets go of it
    // through the back-references. Taken out whenever one is held, saved only
    // while Playing, for onDisconnect's reason: nothing names a body once its
    // session has stopped naming it.
    if (session.playing()) persistPlayer(session);
    if (session.entity != NULL_ENTITY) despawnPlayer(session, false);
    revokeTempAdmin(session.connection);
    session.token.clear();
    session.userId.clear();
    session.nextStaleProfileRepairMillis = 0;
    session.onlineRosterRevision = 0;
    // Both of these gate later messages -- `admin` unlocks the console
    // commands, `username` is who the chat and the guild take this connection
    // for -- so an anonymous session must not keep either.
    session.username.clear();
    session.admin = false;
    session.adminTier = kTopAdminTier;
    session.displayName.clear();
    // Last: despawnPlayer() puts the stage back to Authenticated, and this is
    // what the socket actually is now.
    session.stage = SessionStage::Anonymous;
}

void GameServer::sendProfile(Session& session, net::Connection& connection) {
    // The bag and ring the body is playing with (the arena run's, in the
    // ring), and the progression track it is on (the maze's, in the maze) --
    // so the panels show what the flower actually has and the talent menu
    // spends the points the body actually earned.
    PlayerRecord& record = liveRecord(session);
    if (expireTimedHammer(record) && session.playing()) applyAccountToSession(session);
    const bool onMazeTrack = session.playing() && session.realm == Realm::Maze;
    const double trackXp = onMazeTrack ? record.mazeTotalXp : record.totalXp;
    const SkillSet& skills = onMazeTrack ? record.mazeSkills : record.skills;

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Profile));
    w.u32(++session.profileRevision);
    w.str(session.username);
    std::uint8_t accountFlags = 0;
    if (const Account* account = std::as_const(database_).findUserById(session.userId)) {
        if (account->guest) accountFlags |= net::kAccountGuest;
        if (!account->passwordChosen) accountFlags |= net::kAccountNoPassword;
    }
    w.u8(accountFlags);
    w.f64(trackXp);
    w.u16(static_cast<std::uint16_t>(levelFromTotalXp(trackXp).level));
    w.f64(std::max(0.0, record.stars));
    w.u32(onMazeTrack ? 0 : static_cast<std::uint32_t>(record.talentPointBonus));

    const std::size_t stackCountAt = w.reserveU16();
    std::uint16_t stackCount = 0;
    forEachProfileStack(record, [&](std::uint16_t index, Rarity rarity, std::uint32_t count) {
        w.u16(index);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u32(count);
        ++stackCount;
    });
    w.patchU16(stackCountAt, stackCount);

    // The ring the flower is actually wearing, which in the maze is the
    // account's shifted down a tier: a bar advertising the rarity the account
    // owns while the body swings one below it is a bar that lies.
    const Realm realm = session.playing() ? session.realm : Realm::Overworld;
    w.u8(static_cast<std::uint8_t>(kLoadoutSlots));
    for (std::size_t i = 0; i < kLoadoutSlots; ++i) {
        const WornSlot worn = wornSlot(record, i, realm);
        w.u16(worn.petalIndex);
        w.u8(static_cast<std::uint8_t>(worn.rarity));
    }

    w.u32(record.renderFlags);

    // The talent tree. Only branches that have been bought are sent; the
    // balance is derived on both sides from the level, so it is not on the
    // wire at all and cannot arrive disagreeing with the tiers beside it.
    const std::size_t skillCountAt = w.reserveU16();
    std::uint16_t skillCount = 0;
    for (int i = 0; i < kSkillCount; ++i) {
        const int tier = skills.tier[static_cast<std::size_t>(i)];
        if (tier < 0) continue;
        w.u8(static_cast<std::uint8_t>(i));
        w.u8(static_cast<std::uint8_t>(tier));
        ++skillCount;
    }
    w.patchU16(skillCountAt, skillCount);

    // The mob-kill ledger, for the gallery. Mob ids this build does not know
    // are skipped for the wire and left in the record, exactly as the
    // inventory is: a gallery cell is worth less than an account's history.
    const std::size_t killCountAt = w.reserveU16();
    std::uint16_t killCount = 0;
    forEachProfileKill(record, [&](std::uint16_t index, Rarity rarity, std::uint32_t count) {
        w.u16(index);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u32(count);
        ++killCount;
    });
    w.patchU16(killCountAt, killCount);

    // How long until the account may use an oracle again, as a DURATION: the
    // client's clock is not the server's, so a deadline would arrive already
    // wrong by whatever the two disagree by.
    const double wait = oracleWaitMillis(session.userId);
    w.u32(static_cast<std::uint32_t>(clamp<double>(std::ceil(wait), 0.0, 4294967295.0)));
    // And until it may trade again, the same way. A day is 86.4 million
    // milliseconds, well inside the u32.
    const double tradeWait = traderWaitMillis(session.userId);
    w.u32(static_cast<std::uint32_t>(clamp<double>(std::ceil(tradeWait), 0.0, 4294967295.0)));

    // The saved loadouts, for the row the bar shows while K or L is held --
    // only the ones that exist. The ACCOUNT's, even from inside an arena run:
    // presets are never the run's.
    const PlayerRecord* account = database_.findProgress(session.userId);
    std::vector<std::pair<int, const std::vector<std::optional<StoredItem>>*>> presets;
    if (account != nullptr) {
        for (const auto& [name, slots] : account->loadoutPresets) {
            const int preset = loadoutPresetIndex(name);
            if (preset >= 0) presets.emplace_back(preset, &slots);
        }
    }
    w.u8(static_cast<std::uint8_t>(presets.size()));
    for (const auto& [preset, slots] : presets) {
        w.u8(static_cast<std::uint8_t>(preset));
        const std::size_t count = std::min<std::size_t>(slots->size(), kLoadoutSlots);
        w.u8(static_cast<std::uint8_t>(count));
        for (std::size_t i = 0; i < count; ++i) {
            const std::optional<StoredItem>& item = (*slots)[i];
            const std::uint16_t index = item ? content().petalIndex(item->petalType) : kNoPetal;
            w.u16(index == kInvalidIndex ? kNoPetal : index);
            w.u8(static_cast<std::uint8_t>(item ? item->rarity : Rarity::Common));
        }
    }

    // Persistent, per-petal forge progress; unknown old-save fields round-trip in extra.
    const auto ledgerAt = w.reserveU16();
    std::uint16_t ledgerSize = 0;
    for (std::uint16_t i = 0; i < content().petalCount(); ++i) {
        const int completed = clamp(record.extra["titanForgeCounts"][content().petal(i).id].asInt(0), 0, 2);
        if (completed == 0) continue;
        w.u16(i); w.u8(static_cast<std::uint8_t>(completed)); ++ledgerSize;
    }
    w.patchU16(ledgerAt, ledgerSize);

    connection.send(w);
    clearProfileDue(session);
    sendDropBonus(session, connection);
}

void GameServer::clearProfileDue(Session& session) {
    // Whatever was owed is in what was just sent.
    session.stacksDue.clear();
    session.killsDue.clear();
    session.profileDue = false;
}

void GameServer::sendProfileDelta(Session& session, net::Connection& connection) {
    // Read exactly as sendProfile reads them -- the same record, the same
    // track, the same walkers -- so a client that applies this holds what the
    // whole Profile would have given it, and the digests still agree.
    const PlayerRecord& record = liveRecord(session);
    const bool onMazeTrack = session.playing() && session.realm == Realm::Maze;
    const double trackXp = onMazeTrack ? record.mazeTotalXp : record.totalXp;

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::ProfileDelta));
    w.u32(++session.profileRevision);
    w.f64(trackXp);
    w.u16(static_cast<std::uint16_t>(levelFromTotalXp(trackXp).level));
    w.f64(std::max(0.0, record.stars));
    w.u32(onMazeTrack ? 0 : static_cast<std::uint32_t>(record.talentPointBonus));

    w.u16(static_cast<std::uint16_t>(session.stacksDue.size()));
    const auto stackKey = [](std::uint16_t index, Rarity rarity) {
        return (static_cast<std::uint32_t>(index) << 8) | static_cast<std::uint32_t>(rarity);
    };
    std::array<bool, kRarityCount> tiers{};
    std::unordered_map<std::uint32_t, std::size_t> stackGroups;
    stackGroups.reserve(session.stacksDue.size());
    std::vector<std::vector<std::uint32_t>> counts(session.stacksDue.size());
    for (std::size_t i = 0; i < session.stacksDue.size(); ++i) {
        const auto& group = session.stacksDue[i];
        stackGroups.emplace(stackKey(group.index, group.rarity), i);
        tiers[rarityIndex(group.rarity)] = true;
    }
    for (std::size_t tier = 0; tier < tiers.size(); ++tier) {
        if (!tiers[tier]) continue;
        forEachProfileStack(record, [&](std::uint16_t index, Rarity, std::uint32_t count) {
            const auto found = stackGroups.find(stackKey(index, static_cast<Rarity>(tier)));
            if (found != stackGroups.end() && counts[found->second].size() < 0xFF) {
                counts[found->second].push_back(count);
            }
        }, static_cast<Rarity>(tier));
    }
    for (std::size_t i = 0; i < session.stacksDue.size(); ++i) {
        const auto& group = session.stacksDue[i];
        w.u16(group.index);
        w.u8(static_cast<std::uint8_t>(group.rarity));
        // Retain the full Profile's order when both legacy names map here.
        w.u8(static_cast<std::uint8_t>(counts[i].size()));
        for (const auto count : counts[i]) w.u32(count);
    }

    w.u16(static_cast<std::uint16_t>(session.killsDue.size()));
    for (const Session::ProfileCell& cell : session.killsDue) {
        // Mob IDs have one registry index each. Tier aliases can still map to
        // the same cell, so keep the last nonzero value in saved order.
        std::uint32_t last = 0;
        if (cell.index < content().mobCount()) {
            const Json& byTier = record.mobKills[content().mob(cell.index).id];
            for (const auto& name : byTier.keys()) {
                const int count = byTier[name].asInt();
                if (count > 0 && parseRarity(name) == cell.rarity) last = static_cast<std::uint32_t>(count);
            }
        }
        w.u16(cell.index);
        w.u8(static_cast<std::uint8_t>(cell.rarity));
        w.u32(last);
    }

    connection.send(w);
    clearProfileDue(session);
}

void GameServer::sendDueProfiles() {
    // A Profile is the whole account -- every stack and the whole kill ledger,
    // 8 KB at the 90th percentile of the live database and 14 KB at the top --
    // and banking used to send one per kill and per pickup. An AoE tick that
    // kills ten mobs put 140 KB in front of that player's next snapshot, and
    // `banking` stood out in prod's slow-tick log. Now a snapshot tick sends
    // what changed since the last one: the touched stacks and ledger cells,
    // tens of bytes, in the same flush as the snapshot that shows the kill.
    // A client that drifts anyway is caught by ProfileCheck and sent the whole
    // Profile, which is the only repair a delta needs.
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.profileDue) continue;
        if (net::Connection* connection = listener_.find(entry.first)) {
            sendProfileDelta(session, *connection);
        } else {
            clearProfileDue(session);
        }
    }
}

void GameServer::resyncProfile(Session& session, net::Connection& connection) {
    sendProfile(session, connection);
}

void GameServer::sendDropBonus(Session& session, net::Connection& connection) {
    const auto now=dropBonusNow();auto& record=database_.progress(session.userId);
    if(resumeDropBonusAfterMaintenance(record,now))database_.markDirty();
    const auto rotation=static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1));
    if(rotation>=0 && (!record.extra["lockedShopOffers"].isArray() || record.extra["lockedShopOffersRotation"].asDouble(-1)!=rotation)) {
        captureShopLock(record,rotation);database_.markDirty();
    }
    const auto& b=record.extra["dailyDropBonus"];
    const bool claimed=static_cast<std::int64_t>(b["day"].asDouble(-1))>=dropBonusDay(now);
    ByteWriter w;w.u8(static_cast<std::uint8_t>(net::ServerMessage::DailyDropBonusState));
    const auto remaining = dropBonusRemaining(record, now);
    const bool paused = b["pausedMillis"].asDouble() > 0.0;
    w.u32(dropBonusStreak(record,now));
    w.u8(remaining > 0.0 ? clamp(b["multiplier"].asInt(1), 1, 3) : 1);
    w.u32(static_cast<std::uint32_t>(remaining));
    w.boolean(paused);
    w.u8(static_cast<std::uint8_t>(session.playing() ? simulationPacing_.ticksPerSecond() :
                                                       net::kTicksPerSecond));
    w.u32(claimed?static_cast<std::uint32_t>((dropBonusDay(now)+1)*86400000-8LL*3600000-now):0);
    w.i64(static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1)));
    const auto writeOffers = [&](std::vector<ShopOffer> offers, std::int64_t rotation) {
        applyShopStock(record, rotation, offers);
        w.u8(static_cast<std::uint8_t>(offers.size()));
        for (const auto& offer : offers) {
            w.u16(offer.petalIndex); w.u8(static_cast<std::uint8_t>(offer.rarity));
            w.f64(offer.price); w.u8(static_cast<std::uint8_t>(offer.discountPercent));
            w.u16(offer.remaining < 0 ? 65535 : static_cast<std::uint16_t>(offer.remaining));
        }
    };
    const auto current = shopRotation(shopClockNow());
    // A lock preserves the selection, not the stock epoch. Both tabs share
    // this account's per-item allowance for the current global rotation.
    writeOffers(lockedShopOffers(record), current);
    w.i64(current); writeOffers(shopOffers(current), current);
    connection.send(w);
}
void GameServer::handleDropBonus(Session& session, net::Connection& connection, ByteReader& reader) {
    const auto claim=reader.u8();if(!reader.ok()||!session.authenticated()||claim>4)return;
    auto& record=database_.progress(session.userId);
    if(claim==1 && claimDropBonus(record,dropBonusNow())) database_.markDirty();
    if ((claim == 3 || claim == 4) && setDropBonusEnabled(record, dropBonusNow(), claim == 4))
        database_.markDirty();
    if(claim==2){const auto old=record.extra["lockedShopRotation"].asDouble(-1);
        record.extra["lockedShopRotation"]=Json(old<0?static_cast<double>(shopRotation(shopClockNow())):-1.0);
        if(old<0)captureShopLock(record,static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble()));
        else {record.extra["lockedShopOffers"]=Json();record.extra["lockedShopOffersRotation"]=Json();}
        database_.markDirty();}
    sendDropBonus(session,connection);
}

void GameServer::sendDailyStreak(Session& session, net::Connection& connection) {
    const DailyStreakResult streak = database_.processDailyStreak(session.userId);

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::DailyStreak));
    w.u16(static_cast<std::uint16_t>(std::max(0, streak.streak)));
    w.boolean(streak.newDay);
    w.u16(static_cast<std::uint16_t>(std::max(0, streak.todayReward)));
    w.i64(streak.nextClaimAtMillis);
    w.i64(streak.streakExpiresAtMillis);
    connection.send(w);
}

void GameServer::sendNotice(net::Connection& connection, net::NoticeSeverity severity,
                            const std::string& text) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Notice));
    w.u8(static_cast<std::uint8_t>(severity));
    w.str(text);
    connection.send(w);
}

void GameServer::sendShopResult(net::Connection& connection, net::ShopResultKind kind, bool ok,
                                double stars, const std::string& message) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::ShopResult));
    w.u8(static_cast<std::uint8_t>(kind));
    w.boolean(ok);
    w.f64(std::max(0.0, stars));
    w.str(message);
    connection.send(w);
}

void GameServer::broadcastChat(net::ChatChannel channel, const std::string& author,
                               const std::string& text, std::uint32_t speakerNetId, bool relay) {
    if (relay && config_.authorityAuth && (channel == net::ChatChannel::Global ||
            channel == net::ChatChannel::System || channel == net::ChatChannel::Admin)) {
        Json event = Json::object(); event["action"] = "chatAnnouncement";
        event["channel"] = static_cast<int>(channel); event["author"] = author; event["text"] = text;
        config_.authorityAuth(event);
    }
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Chat));
    w.u8(static_cast<std::uint8_t>(channel));
    w.str(author);
    w.str(text);
    w.u32(speakerNetId);
    listener_.each([&](net::Connection& connection) {
        const Session* session = sessionFor(connection.id());
        if (session && session->authenticated()) connection.send(w);
    });

    chatHistory_.push_back({channel, author, text, database_.nowMillis()});
    while (chatHistory_.size() > kChatHistoryLines) chatHistory_.pop_front();
}

void GameServer::sendChatHistory(net::Connection& connection) {
    // Nothing said yet is nothing to send: the client's transcript is already
    // empty.
    if (chatHistory_.empty()) return;
    static_assert(kChatHistoryLines <= 0xFF, "the line count is a u8");
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::ChatHistory));
    w.u8(static_cast<std::uint8_t>(chatHistory_.size()));
    for (const ChatHistoryLine& line : chatHistory_) {
        w.u8(static_cast<std::uint8_t>(line.channel));
        w.str(line.author);
        w.str(line.text);
        w.f64(static_cast<double>(line.sentAtMillis));
    }
    connection.send(w);
}

void GameServer::handleJoin(Session& session, net::Connection& connection, ByteReader& reader) {
    const double width = reader.u16();
    const double height = reader.u16();
    const std::string where = reader.str();
    const std::string name = reader.str();
    const bool wantsPrestige = reader.u8() != 0;
    if (!reader.ok()) return;
    if (!session.authenticated()) return;
    if (session.playing()) return;

    // The nameplate, not the account. Kept on the session so a respawn keeps
    // the name the player typed rather than reverting to their login. An empty
    // lobby field becomes "Unnamed" from sanitizePlayerName — replace that
    // with the account name so logged-in players spawn as themselves.
    session.displayName = sanitizePlayerName(name);
    if ((session.displayName.empty() || session.displayName == "Unnamed") &&
        !session.username.empty()) {
        session.displayName = session.username;
    }

    // Remembered on the session so a respawn returns to the spawn point the
    // player chose, rather than quietly sending them back to the beginner
    // ground. A choice no staged map defines is dropped here, once, with a
    // notice -- rather than silently every time they die.
    session.spawnChoice.clear();
    if (where == kArenaSpawnChoice || where == kMazeSpawnChoice) {
        // The two destinations that are not on any map at all: each is a realm
        // of its own (realm.h), generated rather than authored, so there is no
        // spawn rectangle to look up.
        session.spawnChoice = where;
    } else if (!where.empty() && where != "default") {
        if (worldMaps_.choice(where) != nullptr) {
            session.spawnChoice = where;
        } else if (worldMaps_.door(where) != nullptr) {
            // A door that exists but is not offered: a map marks it
            // `pickable: false`, and it is reached through a pad rather than
            // from the picker. An admin may still name it -- that is how a
            // screenshot rig reaches one -- but anyone else starts where the
            // picker would have let them.
            if (session.admin) {
                session.spawnChoice = where;
            } else {
                sendNotice(connection, net::NoticeSeverity::Warning,
                           "That door is reached from its biome; starting at the default.");
            }
        } else {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "That spawn point is not on this server; starting at the default.");
        }
    }

    // The prestige twin of that door, for a tree that holds the talent. The
    // arena and the maze have no twin, so the flag means nothing there and is
    // dropped rather than refused: the picker hides both while it is set.
    session.prestige = false;
    if (wantsPrestige && session.spawnChoice != kArenaSpawnChoice &&
        session.spawnChoice != kMazeSpawnChoice) {
        if (database_.progress(session.userId).skills.prestigeUnlocked()) {
            session.prestige = true;
        } else {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "Prestige is a talent; starting in the ordinary biome.");
        }
    }

    const Entity entity = spawnPlayer(session);
    if (entity == NULL_ENTITY) {
        sendNotice(connection, net::NoticeSeverity::Bad, "Could not find a spawn point.");
        return;
    }

    const Vec2 viewport = claimableViewport(world_, entity, content(), width, height);
    world_.get<PlayerLocation>(entity).viewport = viewport;

    session.runLoot.clear();
    views_[session.connection].reset();

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::JoinAccepted));
    w.u32(world_.get<NetId>(entity).value);
    w.position(world_.get<Transform>(entity).position);
    w.u32(tick_);
    // Which maze the server is playing: the client builds the same walls from
    // the day number alone.
    w.i64(activeMaze().day());
    // The map the body was put in: which realm, how big it is, and its exact
    // grid. Encoded, so a large map does not sit on the socket's backpressure
    // ceiling; it is the same grid collision runs on and cannot drift from it.
    writeMapGrid(w, *terrain_, world_.get<Transform>(entity).realm);
    connection.send(w);
    if (session.pyramidMapRealm != session.realm) session.pyramidMapRevealed = false;
    session.pyramidMapRealm = session.realm;
    sendPyramidMapState(session);
    // The client's profile is the account's, and a body in the ring plays on
    // the arena kit instead; the maze body is on its own track. Restated here
    // so the panels show what the flower actually has from the first frame.
    sendProfile(session, connection);
}

void GameServer::handleLeave(Session& session, net::Connection& connection) {
    session.pyramidMapRevealed = false;
    if (!session.playing()) return;
    leaveWorld(session);
    sendProfile(session, connection);
}

void GameServer::leaveWorld(Session& session) {
    // On the body, not the stage: a re-auth is about to change whose session
    // this is, and a body left on it past that point would be credited with
    // whatever it picks up by the account that comes next. persistPlayer
    // saves only a Playing session's, so a body the stage had stopped
    // vouching for is taken out unsaved -- see spawnPlayer for why.
    if (session.entity == NULL_ENTITY) return;
    persistPlayer(session);
    // The body first, then the grant: a grantee steering somebody else's
    // flower lets go of it on the way out of the world, and taking the grant
    // first would move their view back onto a flower that is about to go.
    despawnPlayer(session, true);
    revokeTempAdmin(session.connection);
}

void GameServer::handleInput(Session& session, const net::Connection& connection, ByteReader& reader) {
    auto input = net::InputFrame::read(reader);
    if (!reader.ok() || !session.playing()) return;

    // A replayed or reordered input must not move the player twice. TCP gives
    // ordering, but a client is free to send its own duplicates.
    if (input.sequence <= session.lastInputSequence) return;
    // Inputs replace the whole intent. A later adjacent input supersedes this
    // one, but a reliable action between them is an ordering boundary.
    if (auto next = connection.nextBufferedMessage();
        next && next->u8() == static_cast<std::uint8_t>(net::ClientMessage::Input)) {
        const auto newer = net::InputFrame::read(*next);
        if (next->ok() && next->remaining() == 0 && newer.sequence >= input.sequence) return;
    }
    if (!spend(session.inputAllowance)) return;
    session.lastInputSequence = input.sequence;
    session.snapshotVisibility = input.flags & net::kInputVisibilityMask;
    input.flags &= static_cast<std::uint8_t>(~net::kInputVisibilityMask);

    // Which flower the movement, the aim and the two buttons steer. A flower
    // an admin has taken over ignores its own player entirely, whichever half
    // they are in; the admin steers it instead, through whatever half is
    // active NOW, so a splitter switch moves the control with it. Both ends
    // are back-references on the sessions, which keeps this to one lookup on
    // the hottest path the server has.
    Entity steered = session.entity;
    if (session.controlledBy != 0) {
        steered = NULL_ENTITY;
    } else if (session.controlling != 0) {
        const Session* target = sessionFor(session.controlling);
        steered = target != nullptr && target->playing() ? target->entity : NULL_ENTITY;
    }
    if (PlayerInput* state = world_.tryGet<PlayerInput>(steered)) {
        if(session.controlling==0 && session.controlledBy==0 &&
           (std::abs(input.moveAngle-state->current.moveAngle)>.1 ||
            std::abs(input.moveStrength-state->current.moveStrength)>.1 ||
            std::abs(input.aimAngle-state->current.aimAngle)>.1 || input.flags!=state->current.flags))
            session.councilActiveUntil=wallClockMillis_+30000;
        state->current = input;
    }

    // The window the client is drawing rides every input packet, so a resize or
    // a zoom widens what is replicated on the next tick rather than at the next
    // join. Zero means "unchanged", which is what a client that never learned
    // to report it sends.
    //
    // Always onto the sender's OWN body, whatever it is steering: the claim
    // culls this client's stream, decides what this body hears on Local and
    // keeps the spawner from putting mobs down in plain sight of it. An admin
    // watching through another flower is culled to this same claim by
    // replicate(), and the flower they are steering keeps its own player's.
    if (input.viewportWidth > 0 && input.viewportHeight > 0) {
        // Clamped against the loadout as it stands NOW, every frame, so taking
        // the antennae off shrinks the claim on the next packet.
        const Vec2 viewport = claimableViewport(world_, session.entity, content(),
                                                input.viewportWidth, input.viewportHeight);
        if (PlayerLocation* location = world_.tryGet<PlayerLocation>(session.entity)) {
            location->viewport = viewport;
        }
    }
}

namespace {

/// How long a line a player said stays theirs for the repeat check: the
/// same line to the same place inside it is refused. Long enough that a line
/// cannot come round again on the rate limit's every-two-seconds beat for a
/// while, short enough that "gg" after one fight and "gg" after the next
/// both go out.
constexpr double kChatRepeatWindowMillis = 30000.0;

constexpr const char* kBlockedWordNotice =
    "<span style=\"color: #ff8866;\">Your message was not sent: it contains a word that is not "
    "allowed in chat.</span>";

/// True when a chat line names a raid tier as a bare word.
///
/// The reference tests `/\b(super|unique)\b/i`, and the word boundary is the
/// whole point: "supercell" is not a raid call, and neither is a name that
/// happens to contain the letters. Ultra is deliberately absent -- bots treat
/// an ultra as a mob to fight, not as a rally point.
bool mentionsRaidTier(const std::string& text) {
    static const char* const kWords[] = {"super", "unique"};
    const auto wordChar = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    };
    for (const char* word : kWords) {
        const std::size_t length = std::char_traits<char>::length(word);
        for (std::size_t at = 0; at + length <= text.size(); ++at) {
            bool match = true;
            for (std::size_t i = 0; i < length && match; ++i) {
                match = std::tolower(static_cast<unsigned char>(text[at + i])) == word[i];
            }
            if (!match) continue;
            if (at > 0 && wordChar(static_cast<unsigned char>(text[at - 1]))) continue;
            if (at + length < text.size() &&
                wordChar(static_cast<unsigned char>(text[at + length]))) {
                continue;
            }
            return true;
        }
    }
    return false;
}

} // namespace

void GameServer::handleChat(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::string raw = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    const std::string text = sanitizeChat(raw);
    if (text.empty()) return;

    // A leading slash is a command, and a command is answered rather than
    // said. This has to come BEFORE the mute check: a muted player is barred
    // from talking to other players, not from asking the server questions
    // about their own account.
    //
    // It also spends a DIFFERENT budget. The chat allowance exists to stop one
    // player flooding everyone else, and a command's output goes back to its
    // sender alone -- billed to the chat bucket, four commands emptied it and
    // the console became one command every two seconds.
    if (!text.empty() && text[0] == '/') {
        if (!spend(session.commandAllowance)) {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "You are sending commands too quickly.");
            return;
        }
        if (handleChatCommand(session, connection, text)) return;
    }

    // A line with no slash is said to everyone. Local is "/l", which the
    // command dispatch hands back to sayInPublic.
    sayInPublic(session, connection, net::ChatChannel::Global, text);
}

void GameServer::sayInPublic(Session& session, net::Connection& connection,
                             net::ChatChannel channel, const std::string& text) {
    // The body the line is said FROM, so every client can float it over that
    // flower. Zero while the speaker is on the title screen or dead -- there
    // is nothing in the world to anchor a bubble to then, and a Global line
    // is still printed in the transcript.
    std::uint32_t speakerNetId = 0;
    if (session.playing()) {
        if (const NetId* id = world_.tryGet<NetId>(session.entity)) speakerNetId = id->value;
    }
    // Local is measured from the flower, and there is none to measure from.
    // Refused before anything is billed: nothing was said.
    if (channel == net::ChatChannel::Local && speakerNetId == 0) {
        sendSystem(connection, "Local chat reaches the players who can see you, and you are "
                               "not in the world. Switch to Global to be heard from here.");
        return;
    }

    if (!spend(session.chatAllowance)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are sending messages too quickly.");
        return;
    }

    // Everything from here reaches other players, which is exactly what a
    // mute blocks.
    if (speakerMuted(session, connection)) return;

    std::string said = text;
    if (!screenChatLine(session, connection, channel, "", said)) return;

    if (channel == net::ChatChannel::Local) {
        sendLocalChat(session, said, speakerNetId);
    } else {
        broadcastChat(net::ChatChannel::Global, session.username, said, speakerNetId);
    }

    // Somebody saying "super" or "unique" rallies every bot onto the best boss
    // in the world, exactly as the reference's chat handler does. Only those
    // two words: an ultra is a high-tier mob to fight, never a raid to call.
    // No-ops when no qualifying boss exists.
    if (mentionsRaidTier(said)) triggerBotRaid(clockMillis_);
}

bool GameServer::screenChatLine(Session& session, net::Connection& speaker,
                                net::ChatChannel channel, const std::string& to,
                                std::string& text) {
    // Everyone, admins included, for all of it: the client refuses any other
    // host whoever posted it, so letting one through here would only send a
    // picture nobody can see -- and a full admin's line reaches the same
    // players a guest's does.
    ChatImageFilter filtered = filterChatImages(text);
    if (filtered.removed != 0) {
        sendSystem(speaker, "<span style=\"color: #ff8866;\">" +
                                std::string(filtered.removed == 1 ? "Your image was" : "Your images were") +
                                " removed: images must come from a known image host (" +
                                chatImageHostSummary() + ").</span>");
        text = std::move(filtered.text);
    }
    // A line that was only the pictures is not sent as an empty one, and
    // neither is one of markup that draws nothing -- not even a notice, any
    // more than an empty line gets one.
    if (chatLineDrawsNothing(text)) return false;

    // Refused, not starred: nothing of it reaches anyone. The word goes in
    // the log, which is how an admin tells a false match from a real one;
    // the speaker is not shown it.
    const std::string word = blockedWordIn(text);
    if (!word.empty()) {
        std::printf("[chat] refused a line from %s (\"%s\"): %s\n", session.username.c_str(),
                    word.c_str(), text.c_str());
        sendSystem(speaker, kBlockedWordNotice);
        return false;
    }

    if (chatLineFloods(text)) {
        sendSystem(speaker, "<span style=\"color: #ff8866;\">Your message was not sent: it is "
                            "mostly the same thing over and over.</span>");
        return false;
    }

    // The same line to the same place again within the window. Where it went
    // is part of it: whispering "hi" to two players is not saying it twice.
    std::deque<Session::SaidLine>& recent = session.recentLines;
    while (!recent.empty() && wallClockMillis_ - recent.front().atMillis > kChatRepeatWindowMillis) {
        recent.pop_front();
    }
    const std::string key = std::to_string(static_cast<int>(channel)) + ' ' + lowerCase(to) +
                            '\n' + chatRepeatKey(text);
    for (const Session::SaidLine& said : recent) {
        if (said.key != key) continue;
        sendSystem(speaker, "<span style=\"color: #ff8866;\">Your message was not sent: you "
                            "said that a moment ago.</span>");
        return false;
    }
    recent.push_back({key, wallClockMillis_});
    return true;
}

void GameServer::sendLocalChat(const Session& speaker, const std::string& text,
                               std::uint32_t speakerNetId) {
    const Transform* from = world_.tryGet<Transform>(speaker.entity);
    if (from == nullptr) return;
    // Seen at all is enough, not seen whole: a flower half off the edge of a
    // screen is still one its player can see talking.
    const Body* body = world_.tryGet<Body>(speaker.entity);
    const double margin = body != nullptr ? body->radius : 0.0;

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Chat));
    w.u8(static_cast<std::uint8_t>(net::ChatChannel::Local));
    w.str(speaker.username);
    w.str(text);
    w.u32(speakerNetId);
    for (const auto& [id, listener] : sessions_) {
        if (!listener.authenticated() || !listener.playing()) continue;
        // The speaker hears themselves whatever their own box says.
        if (&listener != &speaker) {
            // From the flower the listener is WATCHING, which for an admin
            // steering another player's is that one: the line is heard where
            // its bubble can be seen.
            const Transform* at = world_.tryGet<Transform>(viewpointOf(listener));
            if (at == nullptr || at->realm != from->realm) continue;
            // The listener's own screen, as the client reported it, centred
            // on the body it is watching from -- the box replication streams
            // and the speech bubble is drawn inside.
            Vec2 viewport{kViewportWidth, kViewportHeight};
            if (const PlayerLocation* location = world_.tryGet<PlayerLocation>(listener.entity)) {
                viewport = location->viewport;
            }
            const double dx = std::abs(from->position.x - at->position.x);
            const double dy = std::abs(from->position.y - at->position.y);
            if (dx > viewport.x * 0.5 + margin || dy > viewport.y * 0.5 + margin) continue;
        }
        if (net::Connection* peer = listener_.find(listener.connection)) peer->send(w);
    }
}

void GameServer::handleSetLoadout(Session& session, ByteReader& reader) {
    const std::uint8_t slot = reader.u8();
    const std::uint16_t petalIndex = reader.u16();
    const Rarity rarity = clampRarity(reader.u8());
    if (!reader.ok() || !session.authenticated()) return;
    if (slot >= kLoadoutSlots) return;
    if (petalIndex != kNoPetal && petalIndex >= content().petalCount()) return;
    if (loadoutLockedInMaze(session)) return;

    PlayerRecord& record = liveRecord(session);
    if(petalIndex!=kNoPetal && slot%kLoadoutActiveSlots>=record.skills.loadoutSlots()) {
        if(auto* peer=listener_.find(session.connection))sendNotice(*peer,net::NoticeSeverity::Warning,"请先解锁对应的槽位天赋。");
        return;
    }
    if (record.loadout.size() < kLoadoutSlots) record.loadout.resize(kLoadoutSlots);

    // Equipping must come out of the inventory, or a client can name any petal
    // it likes and simply be given it. The server's bag is the only one that
    // counts: a stack the client shows and this record does not hold was
    // edited into its memory or is a Profile out of date, and either way it is
    // put right.
    if (petalIndex != kNoPetal && !takeFromInventory(record, petalIndex, rarity, 1)) {
        if (net::Connection* connection = listener_.find(session.connection)) {
            resyncProfile(session, *connection);
        }
        return;
    }

    // Whatever was in the slot goes back to the inventory. Doing this after the
    // take, not before, means a failed take cannot also have emptied the slot.
    if (record.loadout[slot].has_value()) {
        const StoredItem& previous = *record.loadout[slot];
        const std::uint16_t previousIndex = content().petalIndex(previous.petalType);
        if (previousIndex != kInvalidIndex) {
            giveToInventory(record, previousIndex, previous.rarity, 1);
        }
    }

    if (petalIndex == kNoPetal) {
        record.loadout[slot].reset();
    } else {
        StoredItem item;
        item.type = "petal";
        item.petalType = content().petal(petalIndex).id;
        item.rarity = rarity;
        record.loadout[slot] = item;
    }
    autoSaveActivePreset(record);
    database_.markDirty();

    if (session.playing()) applyAccountToSession(session);
    if (net::Connection* connection = listener_.find(session.connection)) {
        sendProfile(session, *connection);
    }
}

std::uint32_t GameServer::profileDigest(Session& session) {
    const PlayerRecord& record = liveRecord(session);
    net::ProfileDigest digest;
    forEachProfileStack(record, [&](std::uint16_t index, Rarity rarity, std::uint32_t count) {
        digest.stack(index, static_cast<std::uint8_t>(rarity), count);
    });
    // The bar as sendProfile writes it: the maze's ring is the account's one
    // tier down.
    const Realm realm = session.playing() ? session.realm : Realm::Overworld;
    for (std::size_t i = 0; i < kLoadoutSlots; ++i) {
        const WornSlot worn = wornSlot(record, i, realm);
        digest.slot(i, worn.petalIndex, static_cast<std::uint8_t>(worn.rarity));
    }
    return digest.value();
}

void GameServer::handleProfileCheck(Session& session, net::Connection& connection,
                                    ByteReader& reader) {
    const std::uint32_t revision = reader.u32();
    const std::uint32_t claimed = reader.u32();
    if (!reader.ok() || !session.authenticated()) return;
    if (wallClockMillis_ < session.nextProfileCheckMillis) return;
    session.nextProfileCheckMillis = wallClockMillis_ + kProfileCheckFloorMillis;

    if (session.profileDue || connection.snapshotBacklog() >= kSnapshotBacklogLimit) return;
    if (revision == session.profileRevision) {
        session.nextStaleProfileRepairMillis = 0;
        if (claimed == profileDigest(session)) return;
    } else if (wallClockMillis_ < session.nextStaleProfileRepairMillis) {
        // Checks already in flight may all predate the same repair. Another
        // full bag would push that repair further back and make each check
        // stale again. Retry later if the client really did miss it.
        return;
    }
    sendProfile(session, connection);
    session.nextStaleProfileRepairMillis = wallClockMillis_ + kStaleProfileRepairMillis;
}

void GameServer::handleSwapLoadout(Session& session, ByteReader& reader) {
    const std::uint8_t a = reader.u8();
    const std::uint8_t b = reader.u8();
    if (!reader.ok() || !session.authenticated()) return;
    if (a >= kLoadoutSlots || b >= kLoadoutSlots || a == b) return;
    if (loadoutLockedInMaze(session)) return;

    PlayerRecord& record = liveRecord(session);
    const int slots = record.skills.loadoutSlots();
    if (a % kLoadoutActiveSlots >= slots || b % kLoadoutActiveSlots >= slots) return;
    if (record.loadout.size() < kLoadoutSlots) record.loadout.resize(kLoadoutSlots);
    std::swap(record.loadout[a], record.loadout[b]);
    autoSaveActivePreset(record);
    database_.markDirty();

    if (session.playing()) applyAccountToSession(session);
    if (net::Connection* connection = listener_.find(session.connection)) {
        sendProfile(session, *connection);
    }
}

bool GameServer::loadoutLockedInMaze(Session& session) {
    if (!session.playing() || session.realm != Realm::Maze) return false;
    // The maze body plays a DERIVED ring -- the account's, one rarity down --
    // and the reference locks the loadout for the run rather than let an edit
    // made in maze terms be persisted in regular ones.
    if (net::Connection* connection = listener_.find(session.connection)) {
        sendNotice(*connection, net::NoticeSeverity::Warning,
                   "Your loadout is locked inside the maze.");
    }
    return true;
}

void GameServer::handleSwapLoadoutRows(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    if (loadoutLockedInMaze(session)) return;

    PlayerRecord& record = liveRecord(session);
    if (record.loadout.size() < kLoadoutSlots) record.loadout.resize(kLoadoutSlots);
    bool changed = false;
    for (int i = 0; i < record.skills.loadoutSlots(); ++i) {
        const auto& reserve = record.loadout[i + kLoadoutActiveSlots];
        if (!reserve || reserve->petalType.empty()) continue;
        std::swap(record.loadout[i], record.loadout[i + kLoadoutActiveSlots]);
        changed = true;
    }
    if (!changed) { sendProfile(session, connection); return; }
    autoSaveActivePreset(record);
    database_.markDirty();

    if (session.playing()) applyAccountToSession(session);
    sendProfile(session, connection);
}

void GameServer::handleSaveLoadoutPreset(Session& session, net::Connection& connection,
                                         ByteReader& reader) {
    const std::uint8_t preset = reader.u8();
    if (!reader.ok() || !session.authenticated()) return;
    if (preset >= kLoadoutPresetCount) return;
    // An arena run wears a scratch ring out of a scratch bag, and that is not
    // a loadout worth keeping on the account.
    if (session.arena) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Saved loadouts can't be used in the arena.");
        return;
    }

    // Saving in the maze is allowed: nothing is edited, and the account's own
    // ring -- what the player will have back outside -- is what gets saved.
    PlayerRecord& record = database_.progress(session.userId);
    std::vector<std::optional<StoredItem>> saved(kLoadoutSlots);
    for (std::size_t i = 0; i < saved.size() && i < record.loadout.size(); ++i) {
        const std::optional<StoredItem>& worn = record.loadout[i];
        if (worn && !worn->petalType.empty()) saved[i] = presetItem(worn->petalType, worn->rarity);
    }
    const std::string name = loadoutPresetName(preset);
    record.loadoutPresets[name] = std::move(saved);
    savePresetSkills(record, name);
    database_.markDirty();
    // The profile carries the presets, and the bar's K/L row is drawn from it.
    sendProfile(session, connection);
    sendNotice(connection, net::NoticeSeverity::Good, "Saved loadout " + name + ".");
}

void GameServer::handleLoadLoadoutPreset(Session& session, net::Connection& connection,
                                         ByteReader& reader) {
    const std::uint8_t preset = reader.u8();
    if (!reader.ok() || !session.authenticated()) return;
    if (preset >= kLoadoutPresetCount) return;
    // Loaded against the run's bag, a preset of the account's petals would
    // find none of them and strip the starter ring bare.
    if (session.arena) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Saved loadouts can't be used in the arena.");
        return;
    }
    if (loadoutLockedInMaze(session)) return;

    PlayerRecord& record = database_.progress(session.userId);
    autoSaveActivePreset(record);
    const std::string name = loadoutPresetName(preset);
    // A preset never saved is an empty loadout, and loads as one: every petal
    // goes back into the bag. Copied either way -- equipPreset rewrites the
    // record the preset lives in.
    const auto saved = record.loadoutPresets.find(name);
    const std::vector<std::optional<StoredItem>> slots =
        saved != record.loadoutPresets.end() ? saved->second
                                             : std::vector<std::optional<StoredItem>>(kLoadoutSlots);
    loadPresetSkills(record, name);
    const PresetLoad result = equipPreset(record, slots, record.skills.loadoutSlots());
    record.extra["activeLoadoutPreset"]=static_cast<int>(preset);
    autoSaveActivePreset(record);
    database_.markDirty();

    if (session.playing()) applyAccountToSession(session);
    sendProfile(session, connection);

    std::string text = "Loaded loadout " + name;
    if (result.downgraded > 0 || result.missing > 0) {
        const auto petals = [](int n) { return std::to_string(n) + (n == 1 ? " petal" : " petals"); };
        text += " (";
        if (result.downgraded > 0) text += petals(result.downgraded) + " at a lower rarity";
        if (result.downgraded > 0 && result.missing > 0) text += ", ";
        if (result.missing > 0) text += petals(result.missing) + " not found";
        text += ")";
    }
    text += ".";
    sendNotice(connection,
               result.missing > 0 ? net::NoticeSeverity::Warning : net::NoticeSeverity::Good, text);
}

void GameServer::handleCraft(Session& session, net::Connection& connection, ByteReader& reader, bool quiet) {
    const std::uint16_t petalIndex = reader.u16();
    const Rarity rarity = clampRarity(reader.u8());
    const std::uint32_t count = reader.u32();
    if (!reader.ok() || !session.authenticated()) return;

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::CraftResult));

    PlayerRecord& record = liveRecord(session);
    // Past kMaxStackCount is more than any stack holds, so it is refused as
    // short rather than narrowed into a number that might not be.
    const bool valid = count >= kCraftBatch &&
                       count <= static_cast<std::uint32_t>(kMaxStackCount) &&
                       craftsOutOf(rarity) && petalIndex < content().petalCount() && content().petal(petalIndex).id != "timed_mjolnir";
    if (!valid || !takeFromInventory(record, petalIndex, rarity, static_cast<int>(count))) {
        w.boolean(false);
        w.u16(petalIndex);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u32(0);
        w.u8(0);
        w.str("Not enough petals to craft.");
        w.u32(0); w.u32(0);
        if (!quiet) connection.send(w);
        if (!quiet) resyncProfile(session, connection);
        return;
    }

    // The whole request is crafted as one POOL rather than as one batch per
    // click: every attempt eats five, and the survivors a failure hands back
    // drop straight into the pool to be tried again, until fewer than five are
    // left. That is what makes the returns actually get crafted instead of
    // piling up in the inventory, and it is why the client plays one spin for
    // a staged xN. Bounded: an attempt removes five and returns at most four,
    // so the pool strictly shrinks.
    // Every equipped clover of the tier being crafted nudges the odds up. The
    // reference counts the PRIMARY ten slots only -- the storage row behind
    // them is not equipped and does not help -- and the bonus is stated in
    // percentage points against a 0..100 roll, which is five ten-thousandths
    // of the fraction this ladder is expressed in.
    constexpr double kCloverCraftBonus = 0.0005;
    const std::uint16_t clover = content().petalIndex("clover");
    int clovers = 0;
    if (clover != kInvalidIndex) {
        const std::size_t equipped =
            std::min<std::size_t>(record.loadout.size(), kLoadoutActiveSlots);
        for (std::size_t i = 0; i < equipped; ++i) {
            if (!record.loadout[i].has_value()) continue;
            const StoredItem& slot = *record.loadout[i];
            if (slot.rarity != rarity) continue;
            if (content().petalIndex(slot.petalType) == clover) ++clovers;
        }
    }
    const double chance =
        std::min(1.0, craftSuccessChance(rarity) + kCloverCraftBonus * clovers);

    // Rolled a ROUND at a time rather than an attempt at a time. Every attempt
    // costs at most five, so pool/5 attempts in a row are all affordable
    // however the earlier ones land -- which makes a round exactly that many
    // attempts, and only its totals matter. A failure keeps one to four of its
    // five evenly: a cost of 1 plus two fair bits (0..3), so `losses` failures
    // cost `losses`, plus twice one coin count, plus another. The same
    // distribution as rolling them singly, in a few dozen rounds rather than
    // the ~800 million single rolls a full stack would take -- which, on the
    // tick thread, was seconds of frozen server.
    std::int64_t pool = count;
    std::int64_t crafted = 0;
    std::uint32_t attemptCount = 0;
    while (pool >= kCraftBatch) {
        const std::int64_t attempts = pool / kCraftBatch;
        attemptCount += static_cast<std::uint32_t>(attempts);
        const std::int64_t wins = countSuccesses(rng_, attempts, chance);
        const std::int64_t losses = attempts - wins;
        const std::int64_t lost = losses + 2 * countSuccesses(rng_, losses, 0.5) +
                                  countSuccesses(rng_, losses, 0.5);
        pool -= wins * kCraftBatch + lost;
        crafted += wins;
    }
    // The sub-batch tail, 0..4, and at most a fifth of the request crafted:
    // both back inside an int.
    const int petalsReturned = static_cast<int>(pool);
    const int upgrades = static_cast<int>(crafted);

    if (petalsReturned > 0) giveToInventory(record, petalIndex, rarity, petalsReturned);
    if (upgrades > 0) giveToInventory(record, petalIndex, upgradeRarity(rarity), upgrades);
    database_.markDirty();

    const Rarity made = upgradeRarity(rarity);
    if (upgrades > 0 && (!quiet || made == Rarity::Super))
        announceRareCraft(session, petalIndex, made, upgrades, count - petalsReturned);

    w.boolean(upgrades > 0);
    w.u16(petalIndex);
    w.u8(static_cast<std::uint8_t>(upgrades > 0 ? upgradeRarity(rarity) : rarity));
    w.u32(static_cast<std::uint32_t>(upgrades));
    w.u8(static_cast<std::uint8_t>(petalsReturned));
    w.str(upgrades > 0 ? "" : "The craft failed.");
    w.u32(attemptCount);
    w.u32(count - petalsReturned);
    if (!quiet) connection.send(w);
    if (!quiet) sendProfile(session, connection);
}

void GameServer::handleBulkCraft(Session& session, net::Connection& connection, ByteReader& reader) {
    const unsigned target=reader.u8();
    const unsigned count = reader.u16();
    if (!reader.ok() || count > content().petalCount()) return;
    std::vector<bool> excluded(content().petalCount(), false);
    for (unsigned i = 0; i < count; ++i) {
        const auto id = reader.u16();
        if (!reader.ok() || id >= excluded.size()) return;
        excluded[id] = true;
    }
    if(!reader.ok() || !session.authenticated() || target<1 || target>static_cast<unsigned>(rarityIndex(Rarity::Super)))return;
    if(wallClockMillis_<session.nextBulkCraftMillis)return;
    session.nextBulkCraftMillis=wallClockMillis_+1500;
    auto& record=liveRecord(session);int groups=0;
    for(unsigned tier=0;tier<target;++tier) {
        const auto rarity=static_cast<Rarity>(tier);if(!craftsOutOf(rarity))break;
        for(std::uint16_t petal=0;petal<content().petalCount();++petal) {
            if(excluded[petal] || content().petal(petal).id=="timed_mjolnir" || !petalComesAt(content().petal(petal),rarity))continue;
            const auto owned=record.itemCount(rarity,inventoryKey(petal));
            if(owned<kCraftBatch)continue;
            ByteWriter request;request.u16(petal);request.u8(static_cast<std::uint8_t>(rarity));request.u32(static_cast<std::uint32_t>(std::min<std::int64_t>(owned,kMaxStackCount)));
            ByteReader input(request.data(),request.size());handleCraft(session,connection,input,true);++groups;
        }
    }
    if(groups>0)database_.markDirty();
    sendProfile(session,connection);
    sendNotice(connection,net::NoticeSeverity::Info,groups>0 ? "一键合成完成：已处理目标等级以下的可用材料，装备花瓣保留。" : "目标等级以下没有足够合成的材料。");
}

void GameServer::handleOracleCraft(Session& session, net::Connection& connection,
                                  ByteReader& reader) {
    const std::uint16_t petalIndex = reader.u16();
    const auto rawRarity = reader.u8();
    const Rarity rarity = clampRarity(rawRarity);
    const std::uint16_t quantity = reader.u16();
    if (!reader.ok() || !session.authenticated()) return;

    const auto refuse = [&](const std::string& reason) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::OracleResult));
        w.boolean(false);
        w.u16(petalIndex);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u16(0);
        w.u32(0);
        w.str(reason);
        connection.send(w);
    };

    // The oracle is a creature standing somewhere, so the one thing a client
    // cannot do is ask it from anywhere else. Where the body IS decides, and
    // the NPC is found here rather than named on the wire: a client that could
    // name one could name one it is nowhere near. A corpse is not standing at
    // anything -- the death card is up, and the forge is still in the menu.
    const bool standing = session.playing() && world_.isAlive(session.entity) &&
                          !world_.has<Dead>(session.entity);
    const Transform* body = standing ? world_.tryGet<Transform>(session.entity) : nullptr;
    if (body == nullptr) {
        refuse("You need to be standing at an oracle.");
        return;
    }
    if (npcs_->findService(world_, NpcService::Oracle, body->position, body->realm,
                           kNpcServiceReach + kNpcServiceSlack) == NULL_ENTITY) {
        refuse("You are too far from the oracle.");
        return;
    }

    const int unitCost = oracleCraftCost(rarity);
    if (rawRarity > static_cast<unsigned>(Rarity::Ultra) || quantity == 0 ||
        unitCost <= 0 || petalIndex >= content().petalCount() ||
        !petalComesAt(content().petal(petalIndex), rarity) ||
        !petalComesAt(content().petal(petalIndex), upgradeRarity(rarity))) {
        refuse("The oracle upgrades Common through Ultra only.");
        return;
    }
    const std::int64_t totalCost = static_cast<std::int64_t>(unitCost) * quantity;
    if (totalCost > kMaxStackCount) { refuse("Too many petals."); return; }
    const int cost = static_cast<int>(totalCost);
    PlayerRecord& record = liveRecord(session);
    const Rarity made = upgradeRarity(rarity);
    if (record.itemCount(made, inventoryKey(petalIndex)) > kMaxStackCount - quantity) {
        refuse("The resulting stack would be too large.");
        return;
    }
    if (!takeFromInventory(record, petalIndex, rarity, cost)) {
        refuse("Not enough petals.");
        resyncProfile(session, connection);
        return;
    }
    giveToInventory(record, petalIndex, made, quantity);
    database_.markDirty();

    // A super bought is announced exactly as a super rolled is: the line is
    // about what exists now, not about how lucky anyone got.
    announceRareCraft(session, petalIndex, made, quantity, cost);

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::OracleResult));
    w.boolean(true);
    w.u16(petalIndex);
    w.u8(static_cast<std::uint8_t>(made));
    w.u16(quantity);
    w.u32(static_cast<std::uint32_t>(cost));
    w.str("");
    connection.send(w);
    // The profile carries the new wait, which is what turns the panel's line
    // red the moment the upgrade has landed.
    sendProfile(session, connection);
}

double GameServer::oracleWaitMillis(const std::string& /*userId*/) {
    return 0.0;
}

void GameServer::handleTrade(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::uint16_t petalIndex = reader.u16();
    const Rarity rarity = clampRarity(reader.u8());
    if (!reader.ok() || !session.authenticated()) return;

    const auto refuse = [&](const std::string& reason) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::TradeResult));
        w.boolean(false);
        w.u16(petalIndex);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u16(kNoPetal);
        w.str(reason);
        connection.send(w);
    };

    // Where the body IS decides, exactly as at the oracle: the trader is found
    // here, never named on the wire.
    const bool standing = session.playing() && world_.isAlive(session.entity) &&
                          !world_.has<Dead>(session.entity);
    const Transform* body = standing ? world_.tryGet<Transform>(session.entity) : nullptr;
    if (body == nullptr) {
        refuse("You need to be standing at a trader.");
        return;
    }
    if (npcs_->findService(world_, NpcService::Trader, body->position, body->realm,
                           kNpcServiceReach + kNpcServiceSlack) == NULL_ENTITY) {
        refuse("You are too far from the trader.");
        return;
    }

    // Once a day, per ACCOUNT.
    const double wait = traderWaitMillis(session.userId);
    if (wait > 0.0) {
        refuse(traderCooldownText(wait));
        return;
    }

    // The petals.json flag is the whole rule: every LADDER tier of a tradable
    // petal is taken, apex included, and the coin itself is marked untradable.
    // Never universal: a universal coin would be a universal nobody gave.
    const std::uint16_t coin = content().petalIndex(kTraderCoinPetal);
    if (petalIndex >= content().petalCount() || !content().petal(petalIndex).tradable ||
        rarityIndex(rarity) >= kLadderRarityCount || coin == kInvalidIndex) {
        refuse("The trader will not take that.");
        return;
    }
    PlayerRecord& record = liveRecord(session);
    if (!takeFromInventory(record, petalIndex, rarity, 1)) {
        refuse("You don't have that petal.");
        resyncProfile(session, connection);
        return;
    }
    giveToInventory(record, coin, rarity, 1);
    traderReadyAt_[session.userId] = wallClockMillis_ + kTraderCooldownMillis;
    database_.markDirty();

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::TradeResult));
    w.boolean(true);
    w.u16(petalIndex);
    w.u8(static_cast<std::uint8_t>(rarity));
    w.u16(coin);
    w.str("");
    connection.send(w);
    // The profile carries the new wait: it is what turns the panel's line red
    // and every stack in its grid grey.
    sendProfile(session, connection);
}

double GameServer::traderWaitMillis(const std::string& userId) {
    const auto found = traderReadyAt_.find(userId);
    if (found == traderReadyAt_.end()) return 0.0;
    const double wait = found->second - wallClockMillis_;
    if (wait > 0.0) return wait;
    traderReadyAt_.erase(found);
    return 0.0;
}

void GameServer::handleAssemble(Session& session, net::Connection& connection,
                                ByteReader& reader) {
    const Rarity rarity = clampRarity(reader.u8());
    const std::uint8_t recipeIndex = reader.u8();
    const int count = reader.u16();
    if (!reader.ok() || !session.authenticated()) return;

    const auto reply = [&](int made, const std::string& reason) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::AssembleResult));
        w.boolean(made > 0);
        w.u8(static_cast<std::uint8_t>(rarity));
        w.u8(recipeIndex);
        w.u16(static_cast<std::uint16_t>(made));
        w.str(reason);
        connection.send(w);
    };

    // Where the body IS decides, exactly as at the oracle: the assembler is
    // found here, never named on the wire. The tier only picks WHICH one,
    // for a flower standing between two.
    const bool standing = session.playing() && world_.isAlive(session.entity) &&
                          !world_.has<Dead>(session.entity);
    const Transform* body = standing ? world_.tryGet<Transform>(session.entity) : nullptr;
    if (body == nullptr) {
        reply(0, "You need to be standing at an assembler.");
        return;
    }
    const Entity assembler =
        npcs_->findService(world_, NpcService::Assembler, body->position, body->realm,
                           kNpcServiceReach + kNpcServiceSlack, rarity);
    if (assembler == NULL_ENTITY) {
        reply(0, "You are too far from the assembler.");
        return;
    }

    // It offers ONE recipe, its own (Npc::recipe): asking for another is a
    // window that has not caught up with which assembler it is at.
    const Npc& npc = world_.get<Npc>(assembler);
    if (npc.recipeRefreshAtMillis > 0.0 && wallClockMillis_ >= npc.recipeRefreshAtMillis) {
        reply(0, "The assembler is changing recipes. Try again in a moment.");
        return;
    }
    const std::vector<AssemblyRecipe>& recipes = content().mob(npc.configIndex).npc.recipes;
    if (recipeIndex >= recipes.size() || recipeIndex != npc.recipe) {
        reply(0, "The assembler does not make that.");
        return;
    }
    // Universal is forged at the titan and nowhere else: an assembler an
    // operator stood at universal makes nothing.
    if (rarityIndex(rarity) >= rarityIndex(Rarity::Apex)) {
        reply(0, "The assembler cannot work at that rarity.");
        return;
    }
    if (count <= 0 || count > kAssemblerMaxBatch) {
        reply(0, "The assembler cannot make that many.");
        return;
    }

    // All or nothing: every input is counted before any is taken, so a batch
    // short of its last petal destroys none of the others.
    const AssemblyRecipe& recipe = recipes[recipeIndex];
    const auto takes = [&](std::uint16_t petal) {
        return assemblerInputCount(recipe, content(), rarity, petal, recipe.takes(petal));
    };
    const Rarity outputRarity = recipe.outputRarity.value_or(rarity);
    if (rarityIndex(outputRarity) >= rarityIndex(Rarity::Apex)) {
        reply(0, "Hyper items cannot be created by an assembler.");
        return;
    }
    // Assemblers change kinds at the same tier. Promotions belong to the
    // forge, so neither stale clients nor future data can bypass its costs.
    if (outputRarity != rarity) {
        reply(0, "Assemblers only convert petals at the same rarity; use the titan to upgrade.");
        return;
    }
    PlayerRecord& record = liveRecord(session);
    std::map<std::uint16_t,std::int64_t> produced;
    for (const auto& output : recipe.outputs) produced[output.petal] += output.count * count;
    for (const auto& [petal, amount] : produced) {
        const auto held = record.itemCount(outputRarity,inventoryKey(petal));
        const auto consumed = takes(petal) * count;
        if (!petalComesAt(content().petal(petal),outputRarity) || amount>kMaxStackCount ||
            held-consumed+amount>kMaxStackCount) {
            reply(0, "The output is unavailable at this rarity or its stack is full.");
            return;
        }
    }
    for (const AssemblyRecipe::Stack& input : recipe.inputs) {
        const std::int64_t need = takes(input.petal) * count;
        if (!petalComesAt(content().petal(input.petal),rarity) || need > kMaxStackCount ||
            record.itemCount(rarity, inventoryKey(input.petal)) < need) {
            reply(0, "You don't have enough " + content().petal(input.petal).name + ".");
            resyncProfile(session, connection);
            return;
        }
    }
    // Taken once per distinct petal, everything the recipe names of it at a
    // time; then every output, each as many times over.
    std::vector<std::uint16_t> taken;
    for (const AssemblyRecipe::Stack& input : recipe.inputs) {
        if (std::find(taken.begin(), taken.end(), input.petal) != taken.end()) continue;
        takeFromInventory(record, input.petal, rarity,
                          static_cast<int>(takes(input.petal) * count));
        taken.push_back(input.petal);
    }
    for (const AssemblyRecipe::Stack& output : recipe.outputs) {
        giveToInventory(record, output.petal, outputRarity,
                        static_cast<int>(std::min<std::int64_t>(output.count * count,
                                                                kMaxStackCount)));
    }
    database_.markDirty();

    reply(count, "");
    // The profile carries the new stacks, which is what refills the window's
    // seats and greys whatever is now short.
    sendProfile(session, connection);
    std::uint32_t consumed = 0;
    for (const auto petal : taken) consumed += static_cast<std::uint32_t>(takes(petal) * count);
    for (const auto& output : recipe.outputs)
        announceRareCraft(session, output.petal, outputRarity,
                          static_cast<std::uint32_t>(output.count * count), consumed);
}

void GameServer::handleAbsorb(Session& session, net::Connection& connection,
                              ByteReader& reader) {
    struct Stack {
        std::uint16_t petalIndex = kNoPetal;
        Rarity rarity = Rarity::Common;
        std::int64_t count = 0;
    };
    const auto mode = reader.u8();
    const int stackCount = reader.u16();
    std::vector<Stack> stacks;
    stacks.reserve(static_cast<std::size_t>(std::min(stackCount, kMaxAbsorbStacks)));
    bool malformed = mode != 0 || stackCount <= 0 || stackCount > kMaxAbsorbStacks;
    for (int i = 0; i < stackCount && !malformed; ++i) {
        Stack stack;
        stack.petalIndex = reader.u16();
        // Read raw rather than through clampRarity: a byte past the ladder is
        // a request to refuse, not one to quietly read as a universal.
        const std::uint8_t rarity = reader.u8();
        stack.count = reader.u32();
        if (!reader.ok()) break;
        if (rarity >= kRarityCount) {
            malformed = true;
            break;
        }
        stack.rarity = static_cast<Rarity>(rarity);
        // One stack per petal and tier: a request naming one twice is asking
        // for the sum of them, and is checked against the bag as that.
        const auto same = std::find_if(stacks.begin(), stacks.end(), [&](const Stack& other) {
            return other.petalIndex == stack.petalIndex && other.rarity == stack.rarity;
        });
        if (same != stacks.end()) same->count += stack.count;
        else stacks.push_back(stack);
    }
    if (!reader.ok() || !session.authenticated()) return;

    const auto reply = [&](bool ok, double xp, std::int64_t absorbed, const std::string& reason) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::AbsorbResult));
        w.boolean(ok);
        w.f64(xp);
        w.u32(static_cast<std::uint32_t>(std::clamp<std::int64_t>(absorbed, 0, UINT32_MAX)));
        w.str(reason);
        connection.send(w);
    };
    if (malformed) {
        reply(false, 0, 0, mode != 0 ? "花瓣回收换星已关闭。" : "Nothing to absorb.");
        return;
    }

    // All or nothing: every stack is checked before any is taken, so a
    // request one petal short destroys none of the others.
    PlayerRecord& record = liveRecord(session);
    double units = 0.0;
    std::int64_t absorbed = 0;
    for (const Stack& stack : stacks) {
        if (stack.petalIndex >= content().petalCount() || content().petal(stack.petalIndex).id == "timed_mjolnir" || stack.count <= 0 ||
            stack.count > kMaxStackCount) {
            reply(false, 0, 0, "Nothing to absorb.");
            return;
        }
        const PetalConfig& petal = content().petal(stack.petalIndex);
        if (!absorbable(petal, stack.rarity)) {
            reply(false, 0, 0, petal.name + " cannot be absorbed.");
            return;
        }
        if (record.itemCount(stack.rarity, inventoryKey(stack.petalIndex)) < stack.count) {
            reply(false, 0, 0, "You don't have enough " + petal.name + ".");
            resyncProfile(session, connection);
            return;
        }
        units += static_cast<double>(stack.count) * absorbUnits(stack.rarity);
        absorbed += stack.count;
    }
    for (const Stack& stack : stacks) {
        takeFromInventory(record, stack.petalIndex, stack.rarity, static_cast<int>(stack.count));
    }

    // Priced by the tree the flower is playing on -- the maze's in the maze,
    // the arena run's (which buys nothing) in the ring -- which is the tree
    // the Profile shows, so the "+N XP" the window printed is what lands.
    const bool onMazeTrack = session.playing() && session.realm == Realm::Maze;
    const double xp = absorbXpTotal(units, onMazeTrack ? record.mazeSkills : record.skills);
    PlayerRecord& account = database_.progress(session.userId);
    // Onto the body whenever there is one, as a kill's XP is: it is the XP
    // authority while it exists, and the record is brought level with it
    // straight after so the Profile below already shows the new level. A
    // living flower levels up on the spot -- health and body damage -- and a
    // corpse simply holds the XP until the respawn reads it back out of the
    // record. With no body at all the account's own track takes it.
    const bool hasBody = session.playing() && world_.isAlive(session.entity) &&
                         world_.has<PlayerProgress>(session.entity);
    if (xp > 0.0) {
        if (hasBody && !world_.has<Dead>(session.entity)) {
            CombatSystem::grantXp(world_, session.entity, xp);
            persistPlayer(session);
        } else if (hasBody) {
            PlayerProgress& progress = world_.get<PlayerProgress>(session.entity);
            progress.totalXp += xp;
            progress.level = levelFromTotalXp(progress.totalXp).level;
            persistPlayer(session);
        } else {
            account.totalXp += xp;
        }
    }
    database_.markDirty();

    reply(true, xp, absorbed, "");
    sendProfile(session, connection);
}

static std::string titanForgeScope(const WorldMaps& maps,Realm realm){
    const auto* map=maps.forRealm(realm);
    return map&&map->templateId()=="garden"?"garden":"";
}
void GameServer::handleTitanForge(Session& session, net::Connection& connection,
                                  ByteReader& reader) {
    const std::uint16_t petalIndex = reader.u16();
    const auto forgeMode=reader.u8();
    const bool hyper=forgeMode==1;
    const bool eternalFragment=forgeMode==2;
    const bool timedHammer=forgeMode==3;
    const bool batch = forgeMode == 4;
    const bool permanentHyper = forgeMode == kPermanentHyperForgeMode;
    const bool all = forgeMode == kTitanForgeAllMode;
    if (forgeMode > kTitanForgeAllMode || (all && petalIndex != kNoPetal)) return;
    if (!reader.ok() || !session.authenticated()) return;

    std::uint16_t outputIndex = petalIndex;
    const auto reply = [&](bool ok, const std::string& reason, int quantity = 1) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::TitanForgeResult));
        w.boolean(ok);
        w.u16(ok ? outputIndex : petalIndex);
        w.str(reason);
        w.u32(ok ? static_cast<std::uint32_t>(quantity) : 0);
        connection.send(w);
    };

    // Where the body IS decides, exactly as at the oracle: the titan is found
    // here, never named on the wire.
    const bool standing = session.playing() && world_.isAlive(session.entity) &&
                          !world_.has<Dead>(session.entity);
    const Transform* body = standing ? world_.tryGet<Transform>(session.entity) : nullptr;
    if (body == nullptr) {
        reply(false, "You need to be standing at the titan.");
        return;
    }
    if (npcs_->findService(world_, NpcService::Titan, body->position, body->realm,
                           kNpcServiceReach + kNpcServiceSlack) == NULL_ENTITY) {
        reply(false, "You are too far from the titan.");
        return;
    }
    if (all) {
        if (wallClockMillis_ < session.nextTitanForgeAllMillis) {
            reply(false, "锻造请求过快，请稍候。");
            return;
        }
        session.nextTitanForgeAllMillis = wallClockMillis_ + 1500.0;
        auto& record = liveRecord(session);
        int kinds = 0;
        std::uint64_t made = 0;
        std::uint64_t consumed = 0;
        for (std::uint16_t index = 0; index < content().petalCount(); ++index) {
            const auto& id = content().petal(index).id;
            if (!titanForgeEligible(id)) continue;
            const auto key = inventoryKey(index);
            const int owned = record.itemCount(kTitanInputRarity, key);
            if (owned == 0) continue;
            const int completed = clamp(record.extra["titanForgeCounts"][id].asInt(0), 0, 2);
            const int space = kMaxStackCount - record.itemCount(kTitanOutputRarity, key);
            const auto exchange = titanForgeBatch(owned, space, completed, id);
            if (!exchange.quantity ||
                !takeFromInventory(record, index, kTitanInputRarity, exchange.cost)) continue;
            giveToInventory(record, index, kTitanOutputRarity, exchange.quantity);
            record.extra["titanForgeCounts"][id] = std::min(2, completed + exchange.quantity);
            announceRareCraft(session, index, kTitanOutputRarity, exchange.quantity, exchange.cost, true, false);
            ++kinds;
            made += exchange.quantity;
            consumed += exchange.cost;
        }
        if (kinds == 0) {
            reply(false, "背包里没有可锻造的超级材料。");
            resyncProfile(session, connection);
            return;
        }
        database_.markDirty();
        const auto result = "一键锻造完成：共 " + std::to_string(kinds) + " 种，获得 " +
            std::to_string(made) + " 个永恒，消耗 " + std::to_string(consumed) + " 个超级。";
        reply(true, result, kinds);
        sendProfile(session, connection);
        return;
    }
    if (petalIndex >= content().petalCount()) {
        reply(false, "The titan cannot forge that.");
        return;
    }

    const auto& petal = content().petal(petalIndex);
    if (permanentHyper) {
        if (!permanentHyperForgeEligible(petal.id) || petal.isAdminPetal ||
            !petalComesAt(petal, Rarity::Unique)) {
            reply(false, "雷神之锤、碎片和血祭不能兑换永久超凡。");
            return;
        }
        PlayerRecord& record = liveRecord(session);
        if (record.itemCount(Rarity::PermanentHyper, inventoryKey(petalIndex)) >= kMaxStackCount) {
            reply(false, "永久超凡堆叠已满。");
            return;
        }
        if (!takeFromInventory(record, petalIndex, Rarity::Unique, kPermanentHyperCost)) {
            reply(false, "需要 30 个未装备的同种永恒花瓣。");
            resyncProfile(session, connection);
            return;
        }
        giveToInventory(record, petalIndex, Rarity::PermanentHyper, 1);
        database_.markDirty();
        announceRareCraft(session, petalIndex, Rarity::PermanentHyper, 1, kPermanentHyperCost, true);
        reply(true, "");
        sendProfile(session, connection);
        return;
    }
    if (eternalFragment && petal.id != "fragment") { reply(false,"该配方仅用于永恒碎片。"); return; }
    if (timedHammer && petal.id != "fragment") { reply(false,"限时锤配方仅接受超级碎片。"); return; }
    if (!(hyper ? hyperForgeEligible(petal.id) : titanForgeEligible(petal.id))) {
        reply(false, hyper ? "超凡锤子需10个永恒碎片，不能用限时永恒锤或血祭。" : "该花瓣不能用于泰坦锻造。");
        return;
    }
    if(hyper) {
        const auto titan=titanForgeScope(worldMaps_,body->realm);
        outputIndex=content().petalIndex(hyperForgeOutput(petal.id));
        if(outputIndex>=content().petalCount()){reply(false,"超凡产物不存在。");return;}
        if(config_.authorityAuth) {
            Json request=Json::object();request["action"]="forge";request["input"]=petal.id;request["titan"]=titan;
            if(!beginSharedHyper(session,connection,request,petalIndex,true))reply(false,"超凡事务正在处理，请稍后重试。");
            return;
        }
        if (pendingLocalHyper_) {
            reply(false, "超凡事务正在处理，请稍后重试。");
            return;
        }
        const auto state=hyperForgeState(database_,hyperForgeOutput(petal.id),titan);
        if(state.owner==session.userId){reply(false,"你已经持有该超凡花瓣。");return;}
        if(state.abandonedBy==session.userId){
            reply(false,kHyperAbandonedRefusal);
            return;
        }
        if(state.protectedUntil>database_.nowMillis()){
            reply(false,"保护期尚未结束，剩余"+std::to_string((state.protectedUntil-database_.nowMillis()+59999)/60000)+"分钟。");return;
        }
        const int cost=hyperForgeCost(state.claims,petal.id);
        if(liveRecord(session).itemCount(Rarity::Unique,inventoryKey(petalIndex))<cost){
            reply(false,"需要"+std::to_string(cost)+"个未装备的同种永恒材料。");return;
        }
        if (database_.authoritative()) {
            persistAll();
        } else {
            // Capture live progress and boss clocks in memory. The forge's
            // durable save publishes those together with the ownership transfer.
            captureLiveProgress();
            captureBossClocksForSave();
        }
        if (database_.backgroundSaveEnabled()) {
            auto plan = planHyperForge(database_, session.userId, petal.id, database_.nowMillis(), {}, titan);
            if (!plan.result.ok) { reply(false, plan.result.reason); return; }
            auto pending = std::make_unique<PendingLocalHyper>();
            pending->capturePlan(database_, std::move(plan));
            pending->titan = titan;
            pending->connection = connection.id();
            pending->input = petalIndex;
            pending->output = outputIndex;
            pending->user = session.userId;
            pending->token = session.token;
            pending->startedAt = monotonicMillis();
            pendingLocalHyper_ = std::move(pending);
            return;
        }
        const auto result=forgeHyper(database_,session.userId,petal.id,database_.nowMillis(),{},titan);
        if (database_.backgroundSaveEnabled() && database_.commitUncertain()) {
            // A rename may already have published the transaction. Stop before
            // answering or accepting another mutation against uncertain state.
            persistenceFrozen_ = true;
            running_.store(false);
            return;
        }
        if(!result.ok){reply(false,result.reason);return;}
        notifyLocalHyper(session.userId, petalIndex, result.affected);
        announceRareCraft(session,outputIndex,Rarity::Apex,1,result.consumed,true);
        reply(true,"");return;
    }
    outputIndex = timedHammer ? content().petalIndex("timed_mjolnir") : petalIndex;
    if (outputIndex >= content().petalCount()) { reply(false, "锻造产物不存在。"); return; }
    PlayerRecord& record = liveRecord(session);
    if (expireTimedHammer(record) && session.playing()) applyAccountToSession(session);
    const int completed = clamp(record.extra["titanForgeCounts"][petal.id].asInt(0), 0, 2);
    const int space = kMaxStackCount - record.itemCount(kTitanOutputRarity, inventoryKey(outputIndex));
    if (space <= 0) {
        reply(false, "目标花瓣堆叠已满。");
        return;
    }
    const int firstCost = titanForgeCost(completed, petal.id);
    const auto exchange = batch
        ? titanForgeBatch(record.itemCount(kTitanInputRarity, inventoryKey(petalIndex)),
                          space, completed, petal.id)
        : TitanForgeBatch{1, firstCost};
    if (exchange.quantity == 0 || !takeFromInventory(record, petalIndex, kTitanInputRarity, exchange.cost)) {
        reply(false, "需要 " + std::to_string(firstCost) + " 个同种超级花瓣。");
        resyncProfile(session, connection);
        return;
    }
    if (timedHammer) {
        addHammerLease(record, static_cast<double>(database_.nowMillis()));
        sendNotice(connection, net::NoticeSeverity::Info,"已获得1把限时雷神之锤，每把独立计时1小时，离线也计时。");
    } else giveToInventory(record, outputIndex, kTitanOutputRarity, exchange.quantity);
    record.extra["titanForgeCounts"][petal.id] = std::min(2, completed + exchange.quantity);
    database_.markDirty();
    announceRareCraft(session, outputIndex, kTitanOutputRarity, exchange.quantity, exchange.cost, true);

    reply(true, "", exchange.quantity);
    sendProfile(session, connection);
}

void GameServer::notifyLocalHyper(const std::string& winner, std::uint16_t input,
                                  const std::vector<std::string>& affected) {
    for (auto& [id, other] : sessions_) {
        if (!other.authenticated() ||
            std::find(affected.begin(), affected.end(), other.userId) == affected.end()) continue;
        if (other.playing()) applyAccountToSession(other);
        if (auto* peer = listener_.find(id)) {
            sendProfile(other, *peer);
            if (other.userId != winner)
                sendNotice(*peer, net::NoticeSeverity::Warning, "你的超凡" + content().petal(input).name +
                           "已被其他玩家夺取，已返还原投入材料，净损失1个永恒材料。");
        }
    }
}

void GameServer::pollLocalHyper(bool shuttingDown) {
    if (!pendingLocalHyper_) return;
    auto& pending = *pendingLocalHyper_;
    using Save = Database::StagedSave;
    auto* session = sessionFor(pending.connection);
    auto* peer = listener_.find(pending.connection);
    const bool sameSession = session && peer && session->authenticated() &&
        session->userId == pending.user && session->token == pending.token;
    const auto stillValid = [&] {
        captureLiveProgress();
        return sameSession &&
            database_.storedTable("hyperForges").dump(0) == pending.expectedForges &&
            std::all_of(pending.expectedPlayers.begin(), pending.expectedPlayers.end(),
                [&](const auto& row) { return database_.playerSnapshotText(row.first) == row.second; });
    };
    std::string refusal = "锻造未提交，材料未扣除，请稍后重试。";
    if (!pending.submitted && !shuttingDown && sameSession && !database_.commitUncertain()) {
        if (!database_.canStageForgeSave()) return;
        bool valid = stillValid();
        if (!valid) {
            const auto& input = content().petal(pending.input).id;
            auto next = planHyperForge(database_, pending.user, input, database_.nowMillis(),
                                       {}, pending.titan);
            valid = next.result.ok;
            if (valid) pending.capturePlan(database_, std::move(next));
            else refusal = next.result.reason;
        }
        if (valid && database_.beginStagedForgeSave(pending.plan.players, pending.forges)) {
            pending.submitted = true;
            // The checkpoint can refresh hold-history timestamps in live rows.
            for (auto& [id, text] : pending.expectedPlayers) text = database_.playerSnapshotText(id);
            return;
        }
    }
    if (shuttingDown && !pending.publishing) database_.decideStagedForgeSave(false);
    const auto outcome = database_.pollStagedForgeSave(shuttingDown);
    if (outcome == Save::Preparing) return;
    if (outcome == Save::Ready) {
        const bool valid = stillValid();
        pending.publishing = valid;
        pending.cancelledForConflict = !valid && sameSession;
        if (valid) pending.publishAt = monotonicMillis();
        database_.decideStagedForgeSave(valid);
        return;
    }
    if (outcome == Save::Uncertain || database_.commitUncertain()) {
        persistenceFrozen_ = true;
        running_.store(false);
        pendingLocalHyper_.reset();
        return;
    }
    // Retry only a stage we deliberately rejected before publishing. A write
    // error or an uncertain commit must never be retried as a conflict.
    if (outcome == Save::Failed && pending.cancelledForConflict && !shuttingDown &&
        sameSession && pending.retriesRemaining > 0) {
        --pending.retriesRemaining;
        pending.submitted = false;
        pending.cancelledForConflict = false;
        return;
    }
    const bool saved = outcome == Save::Saved;
    if (config_.tickProbe && pending.publishAt > 0) {
        std::printf("[titan-commit] saved=%d stageMs=%.2f publishMs=%.2f\n",
                    saved, pending.publishAt - pending.startedAt, monotonicMillis() - pending.publishAt);
    }
    if (saved) {
        // Publication is fenced: nothing can edit these records between the
        // validation and this point. Other progress remains in the live DB.
        for (auto& [id, row] : pending.plan.players) database_.progress(id) = std::move(row);
        database_.rawTable("hyperForges") = std::move(pending.forges);
        database_.markDirty();
        if (!shuttingDown) {
            notifyLocalHyper(pending.user, pending.input, pending.plan.result.affected);
            if (sameSession)
                announceRareCraft(*session, pending.output, Rarity::Apex, 1, pending.plan.result.consumed, true);
        }
    }
    for (const auto& [id, elapsed] : pending.councilActivityCredits) {
        if (elapsed <= 0 || !database_.findProgress(id)) continue;
        auto& activity = database_.progress(id).extra["councilActiveSeconds"];
        activity = std::max(0.0, activity.asDouble()) + elapsed;
        database_.markDirty();
    }
    if (sameSession && !shuttingDown) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::TitanForgeResult));
        w.boolean(saved);
        w.u16(saved ? pending.output : pending.input);
        w.str(saved ? "" : refusal);
        w.u32(saved ? 1 : 0);
        peer->send(w);
    }
    pendingLocalHyper_.reset();
}

void GameServer::handleTitanHolder(Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const std::uint16_t petalIndex = reader.u16();
    if (!reader.ok() || !session.authenticated()) return;
    if (petalIndex >= content().petalCount()) return;
    // Each answer can be a sweep of every account, so one session gets one
    // every kTitanHolderQueryMillis; the client asks again for one dropped.
    if (wallClockMillis_ < session.nextTitanQueryMillis) return;
    // Asked of the titan, so only where it can be asked: standing at it.
    const bool standing = session.playing() && world_.isAlive(session.entity) &&
                          !world_.has<Dead>(session.entity);
    const Transform* body = standing ? world_.tryGet<Transform>(session.entity) : nullptr;
    if (body == nullptr ||
        npcs_->findService(world_, NpcService::Titan, body->position, body->realm,
                           kNpcServiceReach + kNpcServiceSlack) == NULL_ENTITY) {
        return;
    }
    session.nextTitanQueryMillis = wallClockMillis_ + kTitanHolderQueryMillis;
    if(config_.authorityHyper) {
        Json query=Json::object();query["kind"]="titan";query["petal"]=petalIndex;
        query["input"]=content().petal(petalIndex).id;query["titan"]=titanForgeScope(worldMaps_,body->realm);query["account"]=session.userId;
        queueSharedView(session,connection,query);return;
    }


    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::TitanHolder));
    w.u16(petalIndex);
    const auto state=hyperForgeState(database_,hyperForgeOutput(content().petal(petalIndex).id),titanForgeScope(worldMaps_,body->realm));
    const auto* owner=database_.findUserById(state.owner);
    w.str(owner?owner->username:"");
    w.u32(static_cast<std::uint32_t>(std::max<std::int64_t>(0,state.protectedUntil-database_.nowMillis())));
    w.u8(static_cast<std::uint8_t>(hyperForgeCost(state.claims,content().petal(petalIndex).id)));
    w.u8(static_cast<std::uint8_t>(std::min(255,hyperHoldingCount(database_,session.userId))));
    connection.send(w);
}

bool GameServer::tracksUniversals(const std::string& userId) const {
    // The account's flag, not the session's: the sweeps below reach offline
    // accounts, and a temporary admin grant is lent to a connection, not
    // to the petals an account owns.
    const Account* account = database_.findUserById(userId);
    return account == nullptr || !account->admin;
}

std::string GameServer::universalHolderName(std::uint16_t petalIndex) {
    const auto memo = universalHolders_.find(petalIndex);
    if (memo != universalHolders_.end() &&
        wallClockMillis_ - memo->second.atMillis < kTitanHolderMemoMillis) {
        return memo->second.username;
    }
    // A forge leaves one holder; an operator's `give` can leave more, and then
    // any of them is a flower the titan forged it for as far as it can tell.
    std::string holder;
    for (const std::string& userId : database_.playerIds()) {
        const PlayerRecord* record = database_.findProgress(userId);
        if (record == nullptr || !holdsUniversal(*record, petalIndex)) continue;
        const Account* account = std::as_const(database_).findUserById(userId);
        if (account == nullptr || account->admin) continue;
        holder = account->username;
        break;
    }
    universalHolders_[petalIndex] = HolderMemo{holder, wallClockMillis_};
    return holder;
}

int GameServer::takeUniversals(std::uint16_t petalIndex, const std::string& keeperId) {
    if (petalIndex >= content().petalCount()) return 0;
    const PetalConfig& petal = content().petal(petalIndex);
    const std::string key = inventoryKey(petalIndex);

    // Found with the read-only lookup and changed through progress() only
    // where there is something to take, so the sweep costs no row its cached
    // text (see Database::playerIds).
    std::vector<std::string> holders;
    for (const std::string& userId : database_.playerIds()) {
        if (userId == keeperId) continue;
        const PlayerRecord* record = database_.findProgress(userId);
        if (record != nullptr && holdsUniversal(*record, petalIndex) &&
            tracksUniversals(userId)) {
            holders.push_back(userId);
        }
    }

    std::int64_t taken = 0;
    for (const std::string& userId : holders) {
        PlayerRecord& record = database_.progress(userId);
        const int bagged = record.itemCount(Rarity::Universal, key);
        record.addItem(Rarity::Universal, key, -bagged);
        // Wide: a stack runs to kMaxStackCount, and four apex for each of
        // those does not fit an int.
        std::int64_t lost = bagged;
        // A worn one goes too: the slot empties, as if the player had taken it
        // off and the forge had then taken it out of the bag.
        for (std::optional<StoredItem>& slot : record.loadout) {
            if (slot.has_value() && slot->rarity == Rarity::Universal &&
                slot->petalType == petal.id) {
                slot.reset();
                ++lost;
            }
        }
        if (lost <= 0) continue;
        // A refund past a full stack is lost, as anything landing on a full
        // stack is (PlayerRecord::addItem).
        const int refund = static_cast<int>(
            std::min<std::int64_t>(lost * kTitanForgeRefund, kMaxStackCount));
        giveToInventory(record, petalIndex, Rarity::Apex, refund);
        taken += lost;
        const Account* holder = std::as_const(database_).findUserById(userId);
        std::printf("[TITAN] took %lld universal %s from %s\n", static_cast<long long>(lost),
                    petal.id.c_str(), holder != nullptr ? holder->username.c_str() : userId.c_str());

        // The account may be playing right now: its ring loses the petal and
        // its panels see the refund at once.
        const std::string text = "Another player forged the Universal " + petal.name +
                                 ". You received " + std::to_string(refund) + " Apex " +
                                 petal.name + " back.";
        for (auto& entry : sessions_) {
            Session& other = entry.second;
            if (!other.authenticated() || other.userId != userId) continue;
            if (other.playing()) applyAccountToSession(other);
            if (net::Connection* peer = listener_.find(entry.first)) {
                sendProfile(other, *peer);
                sendNotice(*peer, net::NoticeSeverity::Warning, text);
            }
        }
    }
    if (taken > 0) database_.markDirty();
    return static_cast<int>(std::min<std::int64_t>(taken, kMaxStackCount));
}

void GameServer::announceRareCraft(const Session& session, std::uint16_t petalIndex,
                                   Rarity made, std::uint32_t quantity, std::uint32_t consumed, bool titanForge, bool broadcast) {
    // ONE line however many the batch produced:
    // the reference announces the craft, not each petal it yielded. A
    // universal is never crafted -- it is forged at a titan, one at a time --
    // and says so.
    const bool forged = titanForge || made == Rarity::Universal;
    if (made != Rarity::Super && made != Rarity::Unique && made != Rarity::Apex && !forged) return;
    if (petalIndex >= content().petalCount()) return;

    // In the tier's own colour, the one every other surface draws it in.
    char tierColor[8];
    std::snprintf(tierColor, sizeof tierColor, "#%06x", rarityColor(made));
    const std::string verb = forged ? " has been forged by " : " has been crafted by ";
    const std::string tier = rarityLabel(made);
    // "An Apex" -- and "An Unique", because the reference tests the LABEL's
    // first letter against the five vowels and 'U' is one of them. Reproduced
    // rather than corrected: the line is the browser's, word for word. The
    // universal line is not the browser's, and says "The Universal": there is
    // only ever one of each petal at universal.
    const std::string article =
        forged && made != Rarity::PermanentHyper ? "The"
               : std::string("AEIOUaeiou").find(tier[0]) != std::string::npos ? "An" : "A";
    const std::string petal = content().petal(petalIndex).name;
    const std::string playerName =
        session.displayName.empty() ? session.username : session.displayName;

    // Two renderings of one sentence: chat gets the marked-up one, and the
    // feed stores the flat one, because the panel draws glyph outlines and has
    // no parser to hand a tag to.
    const std::string details = std::string(forged ? " (forged x" : " (crafted x") + std::to_string(quantity) +
        (consumed ? ", consumed x" + std::to_string(consumed) : "") + ")";
    const std::string plain = article + " " + tier + " " + petal + verb + "@" +
                              session.username + " [" + playerName + "]" + details;
    const bool privateCraft = made == Rarity::Super;
    const std::string marked = std::string("<b style=\"color: ") + tierColor + ";\">" +
        article + " " + tier + " " + petal + verb + "<b style=\"color: " +
        (privateCraft ? "yellow" : "#00ff00") + ";\">@" + session.username +
        "</b> [<b style=\"color: " + (privateCraft ? "#00ff00" : "yellow") +
        ";\">" + playerName + "</b>]</b>" + details;
    if (privateCraft) {
        if (auto* recipient = listener_.find(session.connection))
            sendChatTo(*recipient, net::ChatChannel::System, "", marked);
        return;
    }
    if (broadcast) broadcastChat(net::ChatChannel::System, "", marked);

    addNotification(made == Rarity::PermanentHyper ? "permanent_hyper_forge"
                    : made == Rarity::Universal ? "universal_craft"
                    : made == Rarity::Apex   ? "apex_craft"
                                             : "unique_craft",
                    plain);
}

void GameServer::bankKills() {
    std::vector<Bounty::Share> ranked;
    std::vector<Entity> recipients;
    // Connections already credited for the corpse in hand; see the loop.
    std::vector<net::ConnectionId> paidAccounts;
    for (const CombatSystem::DeathRecord& death : combat_->deaths()) {
        if (death.wasPlayer) continue;
        const bool waveEnemy = world_.has<WaveEnemy>(death.entity);
        const MobType* type = world_.tryGet<MobType>(death.entity);
        if (type == nullptr) continue;

        // Participation belongs to the damage ledger even if a non-player
        // delivered the last hit. The drop path already pays these rights.
        // The gallery entry and the star bounty go to every player who earned
        // LOOT rights on the corpse, not to the finisher alone: five flowers
        // that bring down an apex are five apex kills and five lots of 250
        // stars. Same ledger and the same selectLootRecipients() the XP and
        // drop paths call -- squads, per-tier slot cap and 1% damage floor
        // included -- so the three cannot disagree about who was in on a kill.
        ranked.clear();
        if (const Bounty* bounty = world_.tryGet<Bounty>(death.entity)) {
            for (const Bounty::Share& share : bounty->contributors) {
                if (share.damage <= 0.0) continue;
                const Bounty::Share live = refreshedShare(world_, share);
                if (world_.isAlive(live.player) && !world_.has<PlayerTag>(live.player)) continue;
                ranked.push_back(live);
            }
        }
        // Stable, because the ledger is in first-hit order and the reference's
        // sort is specified stable: on an exact damage tie the slot at the cut
        // belongs to whoever landed their damage first.
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const Bounty::Share& a, const Bounty::Share& b) {
                             return a.damage > b.damage;
                         });
        const Health* health = world_.tryGet<Health>(death.entity);
        selectLootRecipients(ranked, lootSlotsForRarity(type->rarity), &squadIndex_, recipients,
                             lootDamageFloor(type->rarity, health != nullptr ? health->max : 0.0));

        const std::string& mobId = content().mob(type->configIndex).id;
        const int stars = starsForKill(type->rarity);

        paidAccounts.clear();
        for (const Bounty::Share& share : ranked) {
            // Only a flower that landed damage enters the gallery: a squad's
            // passenger is paid its drops and XP, but it did not kill this.
            if (std::find(recipients.begin(), recipients.end(), share.player) ==
                recipients.end()) {
                continue;
            }
            // A contributor who has left still holds their slot -- nobody is
            // promoted into the gap -- but there is no account left to pay.
            Session* session = sessionForEntity(share.player);
            if (session == nullptr || session->userId.empty()) continue;
            // ONE PERSON, ONE KILL. A splitter puts two of somebody's flowers
            // on the same corpse, and both of them are in this ledger; paying
            // each would enter the kill in their gallery twice for the price
            // of wearing the petal. The slot is still spent -- the half that
            // ranked into it keeps it, and nobody is promoted into the gap --
            // which is the same rule loot_eligibility.h applies to drops.
            if (std::find(paidAccounts.begin(), paidAccounts.end(), session->connection) !=
                paidAccounts.end()) {
                continue;
            }
            paidAccounts.push_back(session->connection);
            if (waveEnemy) {
                if (dungeons_->inWave(world_, share.player))
                    petals_->onKill(world_, content(), share.player, clockMillis_);
                continue;
            }
            petals_->onKill(world_, content(), share.player, clockMillis_);

            PlayerRecord& record = database_.progress(session->userId);
            record.recordKill(mobId, type->rarity);

            // Stars are the mythic-and-above bounty. Awarded on the live entity
            // rather than the record so the HUD sees them this tick;
            // persistPlayer copies them back the same way it does XP.
            // Silently: the reference pays the bounty and updates the star
            // counter, and says nothing. A notice per mythic kill is a line
            // every few seconds in a mythic zone.
            if (stars > 0) {
                if (PlayerProgress* live = world_.tryGet<PlayerProgress>(share.player)) {
                    live->stars += stars;
                    record.stars = live->stars;
                } else {
                    record.stars += stars;
                }
            }
            database_.markDirty();

            // Owed, not sent: see sendDueProfiles. The stars ride in every
            // delta's header.
            noteDue(session->killsDue, type->configIndex, type->rarity);
            session->profileDue = true;
        }

        if (!waveEnemy) announceBossDefeat(death.entity, *type, ranked);
    }
}

void GameServer::announceBossDefeat(Entity mob, const MobType& type,
                                    const std::vector<Bounty::Share>& ranked) {
    // The tiers announced on the way out are exactly the tiers announced on the
    // way in -- ONE constant, so raising kAnnouncedRarity cannot leave chat
    // mourning a boss nobody was told about. An ultra dies as quietly as it
    // spawned.
    if (rarityIndex(type.rarity) < rarityIndex(kAnnouncedRarity)) return;
    // Nor does a prestige biome's super, whose spawn was never announced
    // either: there it is the bands' everyday stock (shared/game/prestige.h).
    if (const Transform* at = world_.tryGet<Transform>(mob)) {
        if (worldMaps_.isPrestige(at->realm)) return;
    }
    // The head's death is the animal's. A centipede's beads each carry their
    // own ledger and die one by one, and a line per bead would be ten more
    // deaths than the one spawn line chat was given.
    if (content().mob(type.configIndex).chainBody) return;
    // A flower's summon was never announced on the way in, and losing one is
    // not a boss falling -- it simply hatches again.
    if (world_.has<Pet>(mob)) return;
    const std::string& name = content().mob(type.configIndex).name;
    char colorAttribute[32];
    std::snprintf(colorAttribute, sizeof colorAttribute, "#%06x", rarityColor(type.rarity));
    const std::string opening = std::string("<b style=\"color: ") + colorAttribute + ";\">A " +
                                rarityLabel(type.rarity) + " " + name + " has been defeated";

    // Credited to the top damage dealer alone, whatever the kill was shared
    // with: `ranked` is already sorted by damage, so that is its first row.
    // No player to credit -- a mob's kill, or a top dealer who has left --
    // still gets the line, without the "by".
    const Session* session =
        ranked.empty() ? nullptr : sessionForEntity(ranked.front().player);
    if (session == nullptr || !session->authenticated()) {
        broadcastChat(net::ChatChannel::System, "", opening + "!</b>");
        return;
    }

    broadcastChat(net::ChatChannel::System, "",
                  opening + " by " + creditedPlayerMarkup(*session) + "</b>");
}

void GameServer::announceRarePickup(const Session& session, std::uint16_t petalIndex,
                                    Rarity rarity) {
    if (petalIndex >= content().petalCount()) return;
    char colorAttribute[32];
    std::snprintf(colorAttribute, sizeof colorAttribute, "#%06x", rarityColor(rarity));
    // The crown has florr's own line (Chat/PetalFoundNamed/champion_crown),
    // at whatever tier it is picked up.
    if (petalLendsBasic(content().petal(petalIndex))) {
        // broadcastChat(net::ChatChannel::System, "",
        //               std::string("<b style=\"color: ") + colorAttribute + ";\">The " +
        //                   content().petal(petalIndex).name + " has been taken by " +
        //                   creditedPlayerMarkup(session) + "!</b>");
        return;
    }
    if (rarityIndex(rarity) < rarityIndex(Rarity::Apex)) return;
    // "The Universal", as the forge says it: there is only ever one of each
    // petal at universal.
    const char* article = rarity == Rarity::Universal ? "The" : "An";
    broadcastChat(net::ChatChannel::System, "",
                  std::string("<b style=\"color: ") + colorAttribute + ";\">" + article + " " +
                      rarityLabel(rarity) + " " + content().petal(petalIndex).name +
                      " has been collected by " + creditedPlayerMarkup(session) + "!</b>");
}

void GameServer::handleUpgradeSkill(Session& session, net::Connection& connection,
                                    ByteReader& reader) {
    const std::uint8_t rawSkill = reader.u8();
    const int tier = reader.u8();
    if (!reader.ok() || !session.authenticated()) return;
    if (rawSkill >= kSkillCount) return;

    const SkillId id = static_cast<SkillId>(rawSkill);
    PlayerRecord& record = database_.progress(session.userId);
    if (session.playing() && session.realm == Realm::Arena) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Talents are disabled in the arena.");
        return;
    }
    // The maze buys from its own tree with its own points.
    const bool maze = session.playing() && session.realm == Realm::Maze;
    if (maze && id == SkillId::Prestige) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Prestige belongs to the main talent tree.");
        return;
    }
    SkillSet& skills = maze ? record.mazeSkills : record.skills;
    const int points = maze ? record.mazeTalentPoints() : record.talentPoints();
    // Tiers are bought one at a time, in order. Accepting an arbitrary target
    // would let a client skip the tiers below it and pay for one of them.
    if (tier < 0 || tier >= skillTierCount(id) || tier != skills.level(id) + 1) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Talents are bought in order.");
        return;
    }
    if (!skills.prerequisiteMet(id)) {
        const SkillGate& gate = *skillGate(id);
        sendNotice(connection, net::NoticeSeverity::Warning,
                   std::string(skillTierLabel(id, tier)) + " needs " +
                       kSkillLabels[static_cast<std::size_t>(gate.parent)] + " " +
                       std::to_string(gate.parentLevel) + ".");
        return;
    }

    const int cost = skillTierCost(id, tier);
    if (points < cost) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Not enough talent points (need " + std::to_string(cost) + ").");
        return;
    }

    skills.set(id, tier);
    database_.markDirty();
    if (session.playing() && world_.isAlive(session.entity)) applyAccountToSession(session);
    sendProfile(session, connection);
}

void GameServer::handleDowngradeSkill(Session& session, net::Connection& connection,
                                      ByteReader& reader) {
    const std::uint8_t rawSkill = reader.u8();
    const int tier = reader.u8();
    if (!reader.ok() || !session.authenticated()) return;
    if (rawSkill >= kSkillCount) return;

    const SkillId id = static_cast<SkillId>(rawSkill);
    PlayerRecord& record = database_.progress(session.userId);
    if (session.playing() && session.realm == Realm::Arena) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Talents are disabled in the arena.");
        return;
    }
    const bool maze = session.playing() && session.realm == Realm::Maze;
    SkillSet& skills = maze ? record.mazeSkills : record.skills;

    // Must own this node: hold-to-refund only on a lit tile.
    if (tier < 0 || tier >= skillTierCount(id) || skills.level(id) < tier) {
        sendNotice(connection, net::NoticeSeverity::Warning, "That talent is not unlocked.");
        return;
    }

    // Drop this tier and everything above on the branch; gated children whose
    // parent no longer meets their requirement go with it.
    skillRefundTo(skills, id, tier - 1);

    if (!maze && id == SkillId::Prestige && !skills.prestigeUnlocked()) {
        session.prestige = false;
        bool inPrestige = false;
        for (const Entity body : bodiesOf(session)) {
            if (body == NULL_ENTITY || !world_.isAlive(body)) continue;
            const auto* at = world_.tryGet<Transform>(body);
            if (at && worldMaps_.isPrestige(at->realm)) inPrestige = true;
        }
        if (inPrestige) {
            leaveWorld(session);
            ByteWriter out;
            out.u8(static_cast<std::uint8_t>(net::ServerMessage::LeftWorld));
            connection.send(out);
        }
    }

    database_.markDirty();
    if (session.playing() && world_.isAlive(session.entity)) applyAccountToSession(session);
    sendProfile(session, connection);
}

void GameServer::handleResetSkills(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    PlayerRecord& record = database_.progress(session.userId);
    if (session.playing() && session.realm == Realm::Arena) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Talents are disabled in the arena.");
        return;
    }
    if (session.playing() && session.realm == Realm::Maze) {
        record.mazeSkills.clear();
    } else {
        record.skills.clear();
        // Prestige went with the rest of the tree, and a flower may not stay
        // in a prestige biome without it: one there -- either half of a split
        // -- is taken out of the world and its client sent back to the title
        // screen, whose next join starts in the ordinary biome.
        session.prestige = false;
        bool inPrestige = false;
        if (session.playing()) {
            for (const Entity body : bodiesOf(session)) {
                if (body == NULL_ENTITY || !world_.isAlive(body)) continue;
                const Transform* at = world_.tryGet<Transform>(body);
                if (at != nullptr && worldMaps_.isPrestige(at->realm)) inPrestige = true;
            }
        }
        if (inPrestige) {
            leaveWorld(session);
            ByteWriter w;
            w.u8(static_cast<std::uint8_t>(net::ServerMessage::LeftWorld));
            connection.send(w);
            database_.markDirty();
            sendProfile(session, connection);
            sendNotice(connection, net::NoticeSeverity::Info,
                       "Talents reset; every point refunded. Without Prestige you are back "
                       "on the title screen.");
            return;
        }
    }
    database_.markDirty();
    if (session.playing() && world_.isAlive(session.entity)) applyAccountToSession(session);
    sendProfile(session, connection);
    sendNotice(connection, net::NoticeSeverity::Info, "Talents reset; every point refunded.");
}

void GameServer::handleBuyPetal(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::uint16_t petalIndex = reader.u16();
    const Rarity rarity = clampRarity(reader.u8());
    const std::uint8_t offerSlot = reader.u8();
    const std::uint32_t quantity = reader.u32();
    if (!reader.ok() || !session.authenticated()) return;
    if (quantity == 0 || quantity > 10000) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false, 0, "Invalid quantity.");
        return;
    }

    // The client sends what it wants, never what it costs. A price that came
    // off the wire is a price the client chose.
    if (!shopSellsPetal(petalIndex) || !shopSellsRarity(rarity) ||
        !petalComesAt(content().petal(petalIndex), rarity)) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false, 0, "That is not for sale.");
        return;
    }
    // Not from inside the ring. Stars are the ACCOUNT's and so is what they
    // buy, but a flower in the arena is playing out of the run's own bag --
    // so the petal would be bought, charged for, and invisible until the run
    // ended. Refused for the same reason talents are.
    if (session.playing() && session.realm == Realm::Arena) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false,
                       database_.progress(session.userId).stars,
                       "The shop is closed in the arena.");
        return;
    }

    PlayerRecord& record = database_.progress(session.userId);
    // A card off the rotating store costs what that card showed, discount
    // included; anything else costs the full ladder price. The slot is only a
    // claim about WHICH card was clicked -- the offers are regenerated here,
    // and a slot whose petal and tier do not match the current rotation's is a
    // click on a store that has since changed.
    double price = shopPrice(petalIndex, rarity);
    std::int64_t purchaseRotation = shopRotation(shopClockNow());
    if (offerSlot == net::kNoShopOffer && (rarity == Rarity::Ultra || rarity == Rarity::Super)) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false, 0,
                       "U/S花瓣只能购买当前或锁定商店中的商品。");
        return;
    }
    if (offerSlot != net::kNoShopOffer) {
        const auto locked=static_cast<std::int64_t>(record.extra["lockedShopRotation"].asDouble(-1));
        const std::vector<ShopOffer> offers = (locked>=0?lockedShopOffers(record):shopOffers(purchaseRotation));
        const auto slot = static_cast<std::size_t>(offerSlot);
        if (slot >= offers.size() || offers[slot].petalIndex != petalIndex ||
            offers[slot].rarity != rarity) {
            sendShopResult(connection, net::ShopResultKind::Purchase, false, 0,
                           "That offer has expired.");
            return;
        }
        price = offers[slot].price;
    }
    const int remaining = shopStockRemaining(record, purchaseRotation, petalIndex, rarity);
    if (remaining >= 0 && quantity > static_cast<std::uint32_t>(remaining)) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false, 0,
                       "该商品本轮库存不足；下次商店换轮补货，重新锁定或登录不会补货。");
        sendDropBonus(session, connection);
        return;
    }
    // A split flower carries the balance on both halves, and the tick levels
    // them to the HIGHER of the two -- so a charge to the steered half alone
    // was refunded by the parked one on the next tick. Levelled before it is
    // read, and the new balance written onto both below.
    syncSplitProgress(session);
    if (quantity > static_cast<std::uint32_t>(kMaxStackCount - record.itemCount(rarity,inventoryKey(petalIndex)))) {
        sendShopResult(connection,net::ShopResultKind::Purchase,false,0,"Inventory is full.");
        return;
    }
    PlayerProgress* live = session.playing() && world_.isAlive(session.entity)
                               ? world_.tryGet<PlayerProgress>(session.entity)
                               : nullptr;
    const double stars = live ? live->stars : record.stars;
    price *= quantity;
    if (!std::isfinite(price) || stars < price) {
        sendShopResult(connection, net::ShopResultKind::Purchase, false, 0, "Not enough stars.");
        return;
    }

    // The price AS QUOTED, not a cast of it. shopPrice() floors, so this is
    // already whole -- but the ladder reaches 3.5^9 times a base of a hundred
    // million, which is comfortably past what an int can hold and used to trap
    // or wrap on the way to the subtraction.
    record.stars = stars - price;
    if (live) {
        for (const Entity body : bodiesOf(session)) {
            if (body == NULL_ENTITY || !world_.isAlive(body)) continue;
            if (PlayerProgress* progress = world_.tryGet<PlayerProgress>(body)) {
                progress->stars = record.stars;
            }
        }
    }
    giveToInventory(record, petalIndex, rarity, quantity);
    consumeShopStock(record, purchaseRotation, petalIndex, rarity, static_cast<int>(quantity));
    database_.markDirty();
    sendProfile(session, connection);
    sendDropBonus(session, connection);
    sendShopResult(connection, net::ShopResultKind::Purchase, true, 0, {});
}

/// Redeems a star code.
///
/// The codes live in the database's own `codes` table -- the one
/// `/admin generate_code` writes, in the shape the browser build's admin
/// commands wrote it -- so a code minted in either era redeems here without a
/// second registry to keep in step.
void GameServer::handleRedeemCode(Session& session, net::Connection& connection,
                                  ByteReader& reader) {
    const std::string typed = reader.str();
    if (!reader.ok() || !session.authenticated()) return;

    // Trimmed and upper-cased before the lookup, exactly as the browser build
    // normalises it: codes are printed in capitals and pasted with whitespace.
    std::string code = typed;
    code.erase(code.begin(),
               std::find_if(code.begin(), code.end(),
                            [](unsigned char c) { return c > ' '; }));
    code.erase(std::find_if(code.rbegin(), code.rend(),
                            [](unsigned char c) { return c > ' '; })
                   .base(),
               code.end());
    for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    Json& codes = database_.rawTable("codes");
    if (code.empty() || !codes.contains(code)) {
        sendShopResult(connection, net::ShopResultKind::Redeem, false, 0, "Invalid code");
        return;
    }

    // Read through a const alias: Json's mutable operator[] inserts, and a
    // code this call is about to refuse must not grow keys on the way out.
    const Json& stored = const_cast<const Json&>(codes)[code];
    const int maxUses = stored["maxUses"].asInt(0);
    const int uses = stored["uses"].asInt(0);
    // Keyed on the ACCOUNT, not the socket: a player who reconnects is the
    // same person, and a code that reset with the connection would be
    // unlimited to anyone willing to press Play twice.
    Json owners = stored["usedBy"];
    if (!owners.isArray()) owners = Json::array();
    for (const Json& owner : owners.items()) {
        if (owner.asString() == session.userId) {
            sendShopResult(connection, net::ShopResultKind::Redeem, false, 0,
                           "Code already redeemed");
            return;
        }
    }
    if (maxUses > 0 && uses >= maxUses) {
        sendShopResult(connection, net::ShopResultKind::Redeem, false, 0,
                       "Code has reached maximum uses");
        return;
    }

    const double stars = stored["stars"].asDouble(0);
    PlayerRecord& record = database_.progress(session.userId);
    record.stars += stars;
    if (session.playing() && world_.isAlive(session.entity)) {
        if (PlayerProgress* live = world_.tryGet<PlayerProgress>(session.entity)) {
            live->stars = record.stars;
        }
    }

    owners.push(Json(session.userId));
    Json& entry = codes[code];
    entry["uses"] = Json(uses + 1);
    entry["usedBy"] = std::move(owners);
    // A fully spent code is dropped rather than left to be rejected forever,
    // which is what the browser build does with it.
    if (maxUses > 0 && uses + 1 >= maxUses) codes.erase(code);
    database_.markDirty();

    sendProfile(session, connection);
    sendShopResult(connection, net::ShopResultKind::Redeem, true, stars, {});

    // The star is U+2B50, exactly as the browser build wrote it. The row
    // stored here is kept identical to the rows that build stored, which the
    // feed still holds -- the shipped face having no glyph for it is the
    // panel's problem to solve, not a reason to write a different history.
    const std::string playerName =
        session.displayName.empty() ? session.username : session.displayName;
    addNotification("star_code", "Star code \"" + code + "\" redeemed by @" + session.username +
                                    " [" + playerName + "]! +" + numberText(stars) +
                                    " \xE2\xAD\x90 Stars");
}

void GameServer::handleSetSkin(Session& session, net::Connection& connection, ByteReader& reader) {
    const std::uint32_t requested = reader.u32();
    if (!reader.ok() || !session.authenticated()) return;

    if (!isSelectablePlayerSkin(requested)) return;

    PlayerRecord& record = database_.progress(session.userId);
    record.renderFlags = requested;
    // Any choice on the built-in tab, including Default, replaces a custom skin.
    record.equippedSkinId.clear();
    database_.markDirty();
    if (session.playing() && world_.isAlive(session.entity)) {
        PlayerVisuals& visuals = world_.ensure<PlayerVisuals>(session.entity);
        visuals.renderFlags = requested;
        visuals.equippedSkinId.clear();
    }
    sendProfile(session, connection);
}

// --- user-created skins ----------------------------------------------------
//
// The catalog is one shared, public list: anything published renders on every
// screen that sees the author wearing it, which is exactly why nothing a
// client sends is trusted. sanitizeSkin() runs here as well as in the studio,
// and the id, the author and the timestamp are the server's to assign.
//
// Stored as raw JSON under the database's `customSkins` key rather than as a
// typed table, because that key holds the browser build's own skin catalog,
// which existing database files carry: its skins have to load here unchanged.

namespace {

constexpr double kSharedSkinFreshMillis = 20.0 * 60'000.0;

/// The studio's own reply channel. Every outcome the reference reports --
/// success included -- is a chat line from "Skins", not a notice or a toast.
void sendSkinChat(net::Connection& connection, const std::string& text) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Chat));
    w.u8(static_cast<std::uint8_t>(net::ChatChannel::System));
    w.str("Skins");
    w.str(text);
    w.u32(0);   // the studio, not a flower: no bubble over anybody
    connection.send(w);
}

/// A guest account, which may play but may not add to the shared catalog.
///
/// The reference tests `/^User\d{8}$/`; this build's guest minting does not
/// zero-pad (client/app_login.cpp), so the digit run is matched at any length rather
/// than at exactly eight -- otherwise the rule would miss the very accounts it
/// exists to stop.
bool isGuestName(const std::string& name) {
    if (name.size() <= 4 || name.compare(0, 4, "User") != 0) return false;
    for (std::size_t i = 4; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}

/// The studio's starter flower, sampled once. It is the skin everybody starts
/// from, so a publish that looks like it adds nothing to the catalog.
const SkinLook& starterLook() {
    static const SkinLook look = sampleSkinLook(starterSkinShapes());
    return look;
}

/// The catalog, parsed, oldest first: the order that decides which of two
/// look-alikes is the original. Ties -- one millisecond, two publishes -- go to
/// the id, so the answer never depends on the file's key order. Rows the parse
/// leaves without shapes (the browser build's old `data` payloads) are never
/// sent and draw nothing, so they are not anybody's original either.
std::vector<CustomSkin> catalogByAge(const Json& table) {
    std::vector<CustomSkin> skins;
    if (!table.isObject()) return skins;
    for (const std::string& id : table.keys()) {
        CustomSkin skin = skinFromJson(table[id]);
        if (skin.id.empty() || skin.shapes.empty()) continue;
        skins.push_back(std::move(skin));
    }
    std::sort(skins.begin(), skins.end(), [](const CustomSkin& a, const CustomSkin& b) {
        if (a.createdAt != b.createdAt) return a.createdAt < b.createdAt;
        return a.id < b.id;
    });
    return skins;
}

std::string base36(std::uint64_t value) {
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (value == 0) return "0";
    std::string out;
    while (value > 0) {
        out += digits[value % 36];
        value /= 36;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace

void GameServer::broadcastToAuthenticated(const ByteWriter& message) {
    listener_.each([&](net::Connection& connection) {
        const Session* session = sessionFor(connection.id());
        if (session && session->authenticated()) connection.send(message);
    });
}

void GameServer::broadcastDebugStats() {
    // Drained whether or not anyone is listening, so a window that spanned an
    // empty server does not pour its samples into the first client to join.
    const double avgMillis =
        debugTickSamples_ > 0 ? debugTickAccumMillis_ / debugTickSamples_ : 0.0;
    const double maxMillis = debugTickMaxMillis_;
    debugTickAccumMillis_ = 0;
    debugTickMaxMillis_ = 0;
    debugTickSamples_ = 0;

    bool anyone = false;
    for (const auto& entry : sessions_) {
        if (entry.second.authenticated()) { anyone = true; break; }
    }
    if (!anyone) return;

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::DebugStats));
    w.f64(static_cast<double>(residentBytes()));
    w.f64(static_cast<double>(heapBytes()));
    w.f32(static_cast<float>(avgMillis));
    w.f32(static_cast<float>(maxMillis));
    w.u8(static_cast<std::uint8_t>(simulationPacing_.ticksPerSecond()));
    broadcastToAuthenticated(w);
}

bool GameServer::canModeratePublicSkins(const Session& session) const {
    const auto* account = database_.findUserById(session.userId);
    return account && !account->guest && !account->adminBanned &&
        database_.storedTable("skinModerators")[account->id].asBool();
}

void GameServer::broadcastSharedSkinRemoval(const std::string& id) {
    ByteWriter message;
    message.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinDeleted));
    message.str(id);
    broadcastToAuthenticated(message);
}

void GameServer::expireSharedSkinSources() {
    const auto now = database_.nowMillis();
    for (auto& source : sharedSkinSources_) {
        if (!source.available || now - source.capturedAt <= kSharedSkinFreshMillis) continue;
        for (const auto& [id, skin] : source.skins) broadcastSharedSkinRemoval(id);
        source.skins.clear();
        source.available = false;
    }
}

bool GameServer::applySharedSkinSnapshot(const Json& snapshot) {
    const int localRealm = config_.sharedSkinRealm;
    if (localRealm < 1 || localRealm > 3 || snapshot["version"].asInt() != 1 || persistenceFrozen_) return false;
    const auto now = database_.nowMillis();
    for (int realm = 1; realm <= 3; ++realm) {
        if (realm == localRealm) continue;
        auto& current = sharedSkinSources_[realm - 1];
        const auto& entry = snapshot["sources"][std::to_string(realm)];
        const double capturedAt = entry["capturedAt"].asDouble();
        const auto& rows = entry["skins"];
        if (!rows.isArray() || capturedAt <= current.capturedAt || capturedAt > now + 60'000.0 ||
            now - capturedAt > kSharedSkinFreshMillis) continue;

        std::unordered_map<std::string, CustomSkin> next;
        bool valid = true;
        const std::string prefix = "xs" + std::to_string(realm) + "_";
        for (const Json& row : rows.items()) {
            auto skin = skinFromJson(row);
            if (skin.id.empty() || skin.id.size() > 120 || !skin.published || !row["published"].asBool(false) ||
                skin.review != SkinReviewState::Approved || skin.shapes.empty() || next.count(prefix + skin.id)) {
                valid = false;
                break;
            }
            skin.id = prefix + skin.id;
            const std::string id = skin.id;
            next.emplace(id, std::move(skin));
        }
        if (!valid) continue;

        std::unordered_set<std::string> removed;
        for (const auto& [id, skin] : current.skins) if (!next.count(id)) removed.insert(id);
        // The old in-memory catalog is empty just after a restart. Check saved
        // outfits then as well, but only after an authoritative fresh source.
        if (!current.available) for (const auto& userId : database_.playerIds()) {
            const auto* record = database_.findProgress(userId);
            if (record && sharedSkinRealm(record->equippedSkinId) == realm && !next.count(record->equippedSkinId))
                removed.insert(record->equippedSkinId);
        }
        for (const auto& id : removed) {
            unequipCustomSkin(id);
            broadcastSharedSkinRemoval(id);
        }
        if (!removed.empty()) database_.markDirty();
        for (const auto& [id, skin] : next) {
            const auto old = current.skins.find(id);
            if (old != current.skins.end() && old->second.name == skin.name && old->second.author == skin.author &&
                old->second.createdAt == skin.createdAt &&
                skinShapesHash(old->second.shapes) == skinShapesHash(skin.shapes)) continue;
            ByteWriter message;
            message.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
            writeCustomSkin(message, skin);
            broadcastToAuthenticated(message);
        }
        const bool restored = !current.available;
        current = {capturedAt, true, std::move(next)};
        if (restored) listener_.each([&](net::Connection& connection) {
            auto* session = sessionFor(connection.id());
            if (!session || !session->authenticated()) return;
            const auto* record = database_.findProgress(session->userId);
            if (record && sharedSkinRealm(record->equippedSkinId) == realm) sendSkinCatalog(*session, connection);
        });
    }
    return true;
}

void GameServer::sendSkinCatalog(Session& session, net::Connection& connection) {
    // storedTable, not rawTable: this only reads, and rawTable drops the
    // table's cached text (Database::Table). Sent on every sign-in, that made
    // the next save re-dump every custom skin -- the 370 ms `persist` ticks.
    const Json& skins = database_.storedTable("customSkins");
    const PlayerRecord& record = database_.progress(session.userId);

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinCatalog));
    // Skin moderation and the admin console are independent capabilities.
    // Both are rechecked on the server when a request arrives.
    std::uint8_t flags = 0;
    if (effectiveAdmin(session)) flags |= net::SkinCatalogAdmin;
    if (canModeratePublicSkins(session)) flags |= net::SkinCatalogModerator;
    w.u8(flags);
    w.str(record.equippedSkinId);
    const std::size_t at = w.reserveU16();
    std::uint16_t count = 0;
    std::vector<ByteWriter> overflow;
    const auto addSkin = [&](const CustomSkin& skin) {
        ByteWriter entry;
        entry.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
        writeCustomSkin(entry, skin);
        if (w.size() + entry.size() >= net::kMaxFrameBytes) {
            overflow.push_back(std::move(entry));
        } else {
            w.raw(entry.data() + 1, entry.size() - 1);
            ++count;
        }
    };
    for (const std::string& id : skins.keys()) {
        const CustomSkin skin = skinFromJson(skins[id]);
        if (skin.id.empty() || skin.shapes.empty()) continue;
        bool visible = skin.published || lowerCase(skin.author) == lowerCase(session.username);
        if (!visible && skin.review == SkinReviewState::Approved) for (const auto& entry : sessions_) {
            const auto& other = entry.second;
            const auto* wearing = other.authenticated() ? database_.findProgress(other.userId) : nullptr;
            if (wearing && wearing->equippedSkinId == skin.id) {
                visible = true; break;
            }
        }
        if (!visible) continue;
        addSkin(skin);
    }
    for (const auto& source : sharedSkinSources_) for (const auto& [id, skin] : source.skins) addSkin(skin);
    w.patchU16(at, count);
    connection.send(w);
    for (const auto& entry : overflow) connection.send(entry);
    const CustomSkin worn = skinFromJson(skins[record.equippedSkinId]);
    if (!worn.id.empty() && !worn.published && worn.review == SkinReviewState::Approved) {
        ByteWriter appearance;
        appearance.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
        writeCustomSkin(appearance, worn); broadcastToAuthenticated(appearance);
    }
}

void GameServer::handlePublishSkin(Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const std::string name = reader.str();
    const std::uint8_t shapeCount = reader.u8();
    std::vector<SkinShape> shapes;
    shapes.reserve(std::min<std::size_t>(shapeCount, kMaxSkinShapes));
    for (int i = 0; i < shapeCount; ++i) {
        SkinShape shape;
        if (readSkinShape(reader, shape) && shapes.size() < static_cast<std::size_t>(kMaxSkinShapes)) {
            shapes.push_back(std::move(shape));
        }
    }
    const bool published = reader.boolean();
    const std::string existingId = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    if (!existingId.empty()) {
        Json& catalog = database_.rawTable("customSkins");
        const Json& saved = static_cast<const Json&>(catalog)[existingId];
        const bool owner = saved["ownerId"].isString()
            ? saved["ownerId"].asString() == session.userId
            : lowerCase(saved["author"].asString()) == lowerCase(session.username);
        if (!saved.isObject() || !owner) {
            sendSkinChat(connection, "只能发布自己保存的皮肤。"); return;
        }
        CustomSkin skin = skinFromJson(saved);
        if (skin.review != SkinReviewState::Approved) {
            if (!published || skin.review != SkinReviewState::Draft) return;
            skin.review = SkinReviewState::Pending;
            catalog[existingId]["review"] = static_cast<int>(skin.review);
            catalog[existingId]["reviewSubmittedAt"] = static_cast<double>(database_.nowMillis());
            database_.markDirty();
            ByteWriter w;
            w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
            writeCustomSkin(w, skin);
            connection.send(w);
            sendSkinChat(connection, "已提交审核，通过后才能穿戴或公开。");
            return;
        }
        if (skin.published == published) return;
        const SkinLook look = sampleSkinLook(skin.shapes);
        if (published && (skinsLookAlike(look, starterLook()) || findLookAlikeSkin(look))) {
            sendSkinChat(connection, "与已有公开皮肤或初始外观过于相似，请修改后发布。"); return;
        }
        catalog[existingId]["published"] = published;
        skin.published = published;
        if (!published) unequipCustomSkin(existingId, session.userId);
        database_.markDirty();
        ByteWriter w; w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
        writeCustomSkin(w, skin); broadcastToAuthenticated(w);
        sendSkinChat(connection, (published ? "已公开皮肤：" : "已撤回为私密，其他穿戴者已恢复默认：") + skin.name); return;
    }

    if (isGuestName(session.username)) {
        sendSkinChat(connection, "Create a (non-guest) account to publish skins.");
        return;
    }
    const SkinCheck check = sanitizeSkin(name, shapes);
    if (!check.ok()) {
        sendSkinChat(connection, check.error);
        return;
    }

    Json& catalog = database_.rawTable("customSkins");
    // Read through a const view: Json's non-const operator[] CREATES the key
    // it is handed, so a lookup that misses would quietly grow the table.
    const Json& stored = catalog;
    // Authors are matched case-blind, as the reference matches them: the
    // account "Rose" and the author string "rose" are one person.
    const std::string me = lowerCase(session.username);
    int mine = 0;
    for (const std::string& id : stored.keys()) {
        if (lowerCase(stored[id]["author"].asString()) == me) ++mine;
    }
    if (mine >= kMaxSkinsPerUser) {
        sendSkinChat(connection, "You've reached the limit of " +
                                     std::to_string(kMaxSkinsPerUser) +
                                     " published skins. Delete one first.");
        return;
    }

    // Last, because it is the one check that costs anything: the cheap
    // refusals above never sample a thing.
    SkinLook look = sampleSkinLook(check.shapes);
    if (published && skinsLookAlike(look, starterLook())) {
        sendSkinChat(connection, "That's the starter flower. Change it a bit more before "
                                 "publishing.");
        return;
    }
    if (const std::optional<CustomSkin> original = published ? findLookAlikeSkin(look) : std::nullopt) {
        if (lowerCase(original->author) == me) {
            sendSkinChat(connection, "That looks just like your \"" + original->name +
                                         "\". Change it a bit more, or delete that one first.");
        } else {
            sendSkinChat(connection, "That looks just like \"" + original->name + "\" by " +
                                         original->author +
                                         ". Change it a bit more before publishing.");
        }
        return;
    }

    CustomSkin skin;
    // Time plus a random tail, as the reference mints it: the clock alone
    // collides when two players publish in the same millisecond.
    skin.id = "sk_" + base36(static_cast<std::uint64_t>(database_.nowMillis())) + "_" +
              base36(rng_.next() % 2176782336ull);
    skin.name = check.name;
    skin.author = session.username;
    skin.shapes = check.shapes;
    skin.createdAt = static_cast<double>(database_.nowMillis());
    skin.review = published ? SkinReviewState::Pending : SkinReviewState::Draft;
    catalog[skin.id] = skinToJson(skin);
    catalog[skin.id]["ownerId"] = session.userId;
    if (published) catalog[skin.id]["reviewSubmittedAt"] = skin.createdAt;
    database_.markDirty();
    skinLooks_[skin.id] = CachedSkinLook{skinShapesHash(skin.shapes), std::move(look)};

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
    writeCustomSkin(w, skin);
    connection.send(w);
    sendSkinChat(connection, published ? "已提交审核，通过后才能穿戴或公开。"
                                      : "草稿已保存，可在皮肤列表提交审核。");
}

void GameServer::handleEquipSkin(Session& session, net::Connection& connection,
                                 ByteReader& reader) {
    const std::string id = reader.str();
    if (!reader.ok() || !session.authenticated()) return;

    // Read-only, so storedTable: see sendSkinCatalog.
    const Json& catalog = database_.storedTable("customSkins");
    const int origin = sharedSkinRealm(id);
    const bool shared = origin && sharedSkinSources_[origin - 1].skins.count(id);
    if (!id.empty() && (origin ? !shared : !catalog[id].isObject())) {
        sendSkinChat(connection, "That skin no longer exists.");
        if (origin) sendSkinCatalog(session, connection);
        return;
    }

    if (!id.empty() && !shared) {
        const CustomSkin skin = skinFromJson(catalog[id]);
        const bool owner = catalog[id]["ownerId"].isString()
            ? catalog[id]["ownerId"].asString() == session.userId
            : lowerCase(skin.author) == lowerCase(session.username);
        if (skin.review != SkinReviewState::Approved || (!skin.published && !owner)) {
            sendSkinChat(connection, skin.review == SkinReviewState::Approved
                ? "这是私人皮肤，不能使用。" : "审核通过后才能穿戴这张皮肤。");
            sendSkinCatalog(session, connection); return;
        }
        if (!skin.published) {
            ByteWriter appearance;
            appearance.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
            writeCustomSkin(appearance, skin); broadcastToAuthenticated(appearance);
        }
    }
    PlayerRecord& record = database_.progress(session.userId);
    record.equippedSkinId = id;
    // A custom skin replaces any built-in cosmetic so the two cannot fight
    // over the same body.
    if (!id.empty()) record.renderFlags = PlayerRenderNone;
    database_.markDirty();
    if (session.playing() && world_.isAlive(session.entity)) {
        // Onto the live body too, or the change waits for the next respawn:
        // the snapshot reads the component, never the account row.
        PlayerVisuals& visuals = world_.ensure<PlayerVisuals>(session.entity);
        visuals.renderFlags = record.renderFlags;
        visuals.equippedSkinId = id;
    }
}

Json GameServer::localSkinReviewQueue() const {
    const auto& catalog = database_.storedTable("customSkins");
    std::vector<std::pair<double, std::string>> pending;
    for (const auto& id : catalog.keys()) {
        const auto& row = catalog[id];
        if (row["review"].asInt(-1) == static_cast<int>(SkinReviewState::Pending))
            pending.emplace_back(row["reviewSubmittedAt"].asDouble(row["createdAt"].asDouble()), id);
    }
    const auto count = std::min<std::size_t>(24, pending.size());
    std::partial_sort(pending.begin(), pending.begin() + count, pending.end());
    Json rows = Json::array();
    for (std::size_t i = 0; i < count; ++i) {
        auto row = skinToJson(skinFromJson(catalog[pending[i].second]));
        row["reviewSubmittedAt"] = pending[i].first;
        rows.push(std::move(row));
    }
    return rows;
}

void GameServer::sendSkinReviewQueue(net::Connection& connection, const Json& sources) {
    std::vector<std::pair<double, CustomSkin>> pending;
    const auto add = [&](const Json& rows, int origin) {
        for (const auto& row : rows.items()) {
            auto skin = skinFromJson(row);
            if (skin.id.empty() || skin.shapes.empty() || skin.review != SkinReviewState::Pending)
                continue;
            if (origin) skin.id = "xs" + std::to_string(origin) + "_" + skin.id;
            pending.emplace_back(row["reviewSubmittedAt"].asDouble(skin.createdAt), std::move(skin));
        }
    };
    add(localSkinReviewQueue(), 0);
    for (int realm = 1; realm <= 3; ++realm)
        if (realm != config_.sharedSkinRealm) add(sources[std::to_string(realm)], realm);
    const auto count = std::min<std::size_t>(24, pending.size());
    std::partial_sort(pending.begin(), pending.begin() + count, pending.end(),
        [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second.id < b.second.id;
        });
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinReviewQueue));
    w.u8(static_cast<std::uint8_t>(count));
    for (std::size_t i = 0; i < count; ++i) writeCustomSkin(w, pending[i].second);
    connection.send(w);
}

Json GameServer::relaySkinReview(const std::string& token, const Json& request) {
    Json response = Json::object();
    if (config_.skinReviewKey.size() < 32 || !crypto::constantTimeEquals(token, config_.skinReviewKey)) {
        response["error"] = "forbidden";
        return response;
    }
    const auto action = request["action"].asString();
    if (action == "skin-review-list") {
        response["rows"] = localSkinReviewQueue();
    } else if (action == "skin-review-approve" || action == "skin-review-reject") {
        if (pendingLocalHyper_) {
            response["error"] = "来源服正在提交锻造，请稍后重试审核。";
            return response;
        }
        const auto id = request["id"].asString();
        const auto error = decideSkinReview(id, action == "skin-review-approve");
        if (!error.empty()) response["error"] = error;
        else if (database_.authoritative() ? commitBeforeReply() : database_.save())
            response["ok"] = true;
        else response["error"] = "来源服暂未确认存档，请刷新队列。";
    }
    return response;
}

void GameServer::handleSkinReview(Session& session, net::Connection& connection, ByteReader& reader) {
    if (!session.authenticated() || !canModeratePublicSkins(session)) return;
    const auto action = static_cast<net::SkinReviewAction>(reader.u8());
    if (!reader.ok()) return;
    const bool list = action == net::SkinReviewAction::List;
    const bool approve = action == net::SkinReviewAction::Approve;
    if (!list && !approve && action != net::SkinReviewAction::Reject) return;
    const std::string id = list ? "" : reader.str();
    if (!reader.ok()) return;
    const int origin = sharedSkinRealm(id);
    if (origin && (!config_.sharedSkinReview || origin == config_.sharedSkinRealm)) return;
    if (!list && !origin) {
        const auto error = decideSkinReview(id, approve);
        if (!error.empty()) sendSkinChat(connection, error);
    }
    if (!config_.sharedSkinReview) {
        sendSkinReviewQueue(connection);
        return;
    }
    Json request = Json::object();
    request["action"] = origin ? (approve ? "skin-review-approve" : "skin-review-reject")
                               : "skin-review-list";
    if (origin) request["id"] = id.substr(4);
    const auto connectionId = connection.id();
    const auto token = session.token;
    config_.sharedSkinReview(origin, request, [this, connectionId, token, origin](Json response) {
        auto* peer = listener_.find(connectionId);
        auto* current = sessionFor(connectionId);
        if (!peer || !current || !current->authenticated() || current->token != token ||
            !canModeratePublicSkins(*current)) return;
        const auto error = response["error"].asString();
        if (!error.empty()) sendSkinChat(*peer, error);
        else if (origin && response["ok"].asBool()) sendSkinChat(*peer, "已更新来源服的审核结果。");
        if (origin) {
            const auto listAction = static_cast<std::byte>(net::SkinReviewAction::List);
            ByteReader reader(&listAction, 1);
            handleSkinReview(*current, *peer, reader);
        } else {
            sendSkinReviewQueue(*peer, std::as_const(response)["sources"]);
        }
    });
}

std::string GameServer::decideSkinReview(const std::string& id, bool approve) {
    const auto& saved = database_.storedTable("customSkins")[id];
    auto skin = skinFromJson(saved);
    if (skin.id.empty() || skin.review != SkinReviewState::Pending) {
        return "该皮肤已不在待审队列。";
    }
    if (approve) {
        const auto& look = catalogSkinLook(skin);
        if (skinsLookAlike(look, starterLook()) || findLookAlikeSkin(look)) {
            return "这张皮肤和已有公开皮肤或初始外观过于相似，请驳回。";
        }
        skin.review = SkinReviewState::Approved;
    } else {
        skin.review = SkinReviewState::Rejected;
    }
    const std::string ownerId = saved["ownerId"].asString();
    database_.rawTable("customSkins")[id]["review"] = static_cast<int>(skin.review);
    database_.markDirty();
    ByteWriter update;
    update.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinPublished));
    writeCustomSkin(update, skin);
    listener_.each([&](net::Connection& peer) {
        const auto* owner = sessionFor(peer.id());
        if (!owner || !owner->authenticated() || owner->userId != ownerId) return;
        peer.send(update);
        sendSkinChat(peer, approve
            ? "皮肤审核通过，可以穿戴或公开。" : "皮肤审核未通过，请修改后重新保存并提交。");
    });
    return {};
}

void GameServer::unequipCustomSkin(const std::string& id, const std::string& exceptUserId) {
    for (const std::string& userId : database_.playerIds()) {
        const auto* record = database_.findProgress(userId);
        if (userId == exceptUserId || !record || record->equippedSkinId != id) continue;
        auto& changed = database_.progress(userId);
        changed.equippedSkinId.clear();
        changed.renderFlags = PlayerRenderNone;
    }
    for (auto& [connection, session] : sessions_) {
        if (!session.authenticated() || session.userId == exceptUserId) continue;
        if (session.arena && session.arena->equippedSkinId == id) {
            session.arena->equippedSkinId.clear();
            session.arena->renderFlags = PlayerRenderNone;
        }
        for (const Entity body : bodiesOf(session)) {
            auto* visuals = world_.tryGet<PlayerVisuals>(body);
            if (!visuals || visuals->equippedSkinId != id) continue;
            visuals->equippedSkinId.clear();
            visuals->renderFlags = PlayerRenderNone;
        }
    }
}

void GameServer::handleDeleteSkin(Session& session, net::Connection& connection,
                                  ByteReader& reader) {
    const std::string id = reader.str();
    if (!reader.ok() || !session.authenticated()) return;

    const Json& saved = database_.storedTable("customSkins")[id];
    if (!saved.isObject()) return;
    const auto skin = skinFromJson(saved);
    const bool owner = saved["ownerId"].isString() ? saved["ownerId"].asString() == session.userId
        : lowerCase(skin.author) == lowerCase(session.username);
    if (!session.admin && !owner && !(skin.published && canModeratePublicSkins(session))) {
        sendSkinChat(connection, "You do not have permission to delete this skin.");
        return;
    }
    database_.rawTable("customSkins").erase(id);
    skinLooks_.erase(id);
    unequipCustomSkin(id);
    database_.markDirty();

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SkinDeleted));
    w.str(id);
    broadcastToAuthenticated(w);
    sendSkinChat(connection, owner ? "Deleted \"" + skin.name + "\"."
                                   : "Took down \"" + skin.name + "\" by " + skin.author + ".");
}

const SkinLook& GameServer::catalogSkinLook(const CustomSkin& skin) {
    const std::uint64_t hash = skinShapesHash(skin.shapes);
    auto found = skinLooks_.find(skin.id);
    if (found == skinLooks_.end() || found->second.shapesHash != hash) {
        found = skinLooks_.insert_or_assign(skin.id, CachedSkinLook{hash, sampleSkinLook(skin.shapes)})
                    .first;
    }
    return found->second.look;
}

std::optional<CustomSkin> GameServer::findLookAlikeSkin(const SkinLook& look) {
    const Json& table = database_.storedTable("customSkins");
    // Forget the looks of skins that are gone, whoever took them down -- the
    // admin editor erases rows without coming through here.
    for (auto it = skinLooks_.begin(); it != skinLooks_.end();) {
        it = table.isObject() && table.contains(it->first) ? std::next(it) : skinLooks_.erase(it);
    }
    for (CustomSkin& skin : catalogByAge(table)) {
        if (!skin.published) continue;
        if (skinsLookAlike(look, catalogSkinLook(skin))) return std::move(skin);
    }
    return std::nullopt;
}

void GameServer::foldLookAlikeSkins() {
    const std::vector<CustomSkin> skins = catalogByAge(database_.storedTable("customSkins"));
    // Copy id -> the id it folds into, "" for the starter flower.
    std::unordered_map<std::string, std::string> foldedInto;
    std::vector<const CustomSkin*> originals;
    for (const CustomSkin& skin : skins) {
        if (!skin.published) continue;
        const SkinLook& look = catalogSkinLook(skin);
        const CustomSkin* original = nullptr;
        const bool starter = skinsLookAlike(look, starterLook());
        if (!starter) {
            // Against the originals only, never a copy: A, then B like A, then
            // C like B but not like A is two skins, not one.
            for (const CustomSkin* candidate : originals) {
                if (skinsLookAlike(look, catalogSkinLook(*candidate))) {
                    original = candidate;
                    break;
                }
            }
            if (original == nullptr) {
                originals.push_back(&skin);
                continue;
            }
        }
        foldedInto[skin.id] = original != nullptr ? original->id : std::string();
        if (original != nullptr) {
            std::printf("[skins] folded \"%s\" by %s (%s) into \"%s\" by %s (%s)\n",
                        skin.name.c_str(), skin.author.c_str(), skin.id.c_str(),
                        original->name.c_str(), original->author.c_str(), original->id.c_str());
        } else {
            std::printf("[skins] removed \"%s\" by %s (%s): it is the starter flower\n",
                        skin.name.c_str(), skin.author.c_str(), skin.id.c_str());
        }
    }
    if (foldedInto.empty()) return;

    Json& catalog = database_.rawTable("customSkins");
    for (const auto& fold : foldedInto) {
        catalog.erase(fold.first);
        skinLooks_.erase(fold.first);
    }
    // Offline accounts too: a row still naming a copy would come back to a
    // skin that no longer exists, which draws as the plain flower.
    int rehomed = 0;
    for (const std::string& userId : database_.playerIds()) {
        const PlayerRecord* record = database_.findProgress(userId);
        if (record == nullptr || record->equippedSkinId.empty()) continue;
        const auto fold = foldedInto.find(record->equippedSkinId);
        if (fold == foldedInto.end()) continue;
        database_.progress(userId).equippedSkinId = fold->second;
        ++rehomed;
    }
    database_.markDirty();
    std::printf("[skins] folded %zu look-alike skin(s); %d account(s) moved onto the "
                "original\n",
                foldedInto.size(), rehomed);
}

void GameServer::servicePracticeTrials(double nowMillis) {
    for (auto& entry : sessions_) {
        auto& session = entry.second;
        auto& trial = session.practiceTrial;
        if (trial.player == NULL_ENTITY) continue;
        const auto* player = world_.tryGet<Transform>(trial.player);
        const auto* dummy = world_.tryGet<Transform>(trial.dummy);
        const auto* equipped = world_.tryGet<Loadout>(trial.player);
        const bool valid = session.playing() && session.entity == trial.player && !session.split() &&
            player && dummy && equipped && isStanding(world_, trial.player) && player->realm == dummy->realm &&
            distanceSq(player->position, dummy->position) <= 800.0 * 800.0 &&
            practiceLoadoutKey(*equipped) == trial.loadout;
        if (valid && nowMillis < trial.endsAt) continue;
        if (valid) savePracticeScore(database_, session.userId, practiceLoadout(content(), *equipped), trial.damage);
        if (auto* connection = listener_.find(session.connection)) {
            if (valid && trial.damage > 0.0) {
                sendNotice(*connection, net::NoticeSeverity::Good, "10 秒测试结束，成绩默认私密。");
            } else {
                sendNotice(*connection, net::NoticeSeverity::Info, valid ? "本次测试未造成伤害。" :
                    "离开木桩、倒地或换装会取消本次测试。");
            }
        }
        trial = {};
    }
}

void GameServer::handlePracticeBoard(Session& session, net::Connection& connection, ByteReader& reader) {
    const bool includeAdmins = reader.boolean();
    const auto action = static_cast<net::PracticeBoardAction>(reader.u8());
    const auto id = reader.u32();
    if (!reader.ok()) return;
    switch (action) {
        case net::PracticeBoardAction::Start: {
            const auto* at = world_.tryGet<Transform>(session.entity);
            const auto* equipped = world_.tryGet<Loadout>(session.entity);
            Entity target = NULL_ENTITY;
            double closest = 500.0 * 500.0;
            const auto type = content().mobIndex("target_dummy");
            if (at && equipped && session.playing() && !session.split() &&
                isStanding(world_, session.entity)) {
                Query<Npc, Transform>{world_}.each([&](Entity e, Npc& npc, Transform& pos) {
                    if (npc.configIndex != type || npc.rarity != Rarity::Super || pos.realm != at->realm) return;
                    const double gap = distanceSq(at->position, pos.position);
                    if (gap < closest && !terrain_->segmentBlocked(at->position, pos.position, at->realm)) {
                        target = e;
                        closest = gap;
                    }
                });
            }
            if (session.practiceTrial.player != NULL_ENTITY) {
                sendNotice(connection, net::NoticeSeverity::Info, "已有一场木桩测试正在进行。");
            } else if (target == NULL_ENTITY) {
                sendNotice(connection, net::NoticeSeverity::Info, "请靠近丛林训练区的超级木桩，以单个角色开始测试。");
            } else {
                session.practiceTrial = {session.entity, target, clockMillis_ + net::kPracticeWindowSeconds * 1000.0,
                                         0.0, practiceLoadoutKey(*equipped)};
                sendNotice(connection, net::NoticeSeverity::Good, "10 秒木桩测试开始。保持配装并攻击超级木桩。");
            }
            break;
        }
        case net::PracticeBoardAction::Publish:
        case net::PracticeBoardAction::Hide:
            sharePracticeScore(database_, session.userId, id, action == net::PracticeBoardAction::Publish);
            break;
        case net::PracticeBoardAction::Read:
            break;
        default:
            return;
    }
    ByteWriter out;
    writePracticeBoard(out, practiceBoardView(database_, session.userId, includeAdmins));
    connection.send(out);
    if (config_.authorityHyper && action == net::PracticeBoardAction::Read) {
        Json query = Json::object();
        query["kind"] = "practice";
        query["account"] = session.userId;
        query["includeAdmins"] = includeAdmins;
        queueSharedView(session, connection, std::move(query));
    }
}

void GameServer::handleLeaderboard(const Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const bool includeAdmins = reader.boolean();
    const auto requestedOffset = reader.u32();
    if (!reader.ok()) return;
    if(config_.authorityHyper) {
        Json query=Json::object();query["kind"]="leaderboard";query["offset"]=double(requestedOffset);
        query["includeAdmins"]=includeAdmins;query["admin"]=session.admin;query["account"]=session.userId;
        queueSharedView(session,connection,query);return;
    }

    constexpr double kLeaderboardCacheMillis = 5000.0;
    auto& cache = leaderboardCache_[includeAdmins ? 1 : 0];
    if (wallClockMillis_ >= cache.refreshAtMillis) {
        refreshLeaderboard(cache, includeAdmins);
        cache.refreshAtMillis = wallClockMillis_ + kLeaderboardCacheMillis;
    }
    if (wallClockMillis_ >= leaderboardHoldingsRefreshAtMillis_) {
        leaderboardHoldings_ = hyperHoldingBoard(database_, database_.nowMillis());
        leaderboardHoldingsRefreshAtMillis_ = wallClockMillis_ + kLeaderboardCacheMillis;
    }

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Leaderboard));
    w.u8(cache.shown);
    w.u32(cache.users);
    w.u32(session.admin ? cache.activeToday : 0);
    w.raw(cache.rows.data(), cache.rows.size());
    const auto& holdings = leaderboardHoldings_;
    const auto total = static_cast<std::uint32_t>(holdings.size());
    const auto offset = total ? std::min(requestedOffset / kHyperBoardPageSize,
        (total - 1) / kHyperBoardPageSize) * kHyperBoardPageSize : 0;
    const auto end = std::min(total, offset + kHyperBoardPageSize);
    w.u32(total); w.u32(offset); w.u16(static_cast<std::uint16_t>(end - offset));
    for (auto i = offset; i < end; ++i) {
        const auto& row = holdings[i];
        w.str(row.name); w.str(row.petal); w.u32(row.count); w.u32(row.ownerTotal);
    }
    w.raw(cache.playtimes.data(), cache.playtimes.size());
    const auto rankOf = [&](const auto& ranks) {
        const auto found = ranks.find(session.username);
        return found == ranks.end() ? 0u : found->second;
    };
    w.u32(rankOf(cache.levelRanks));
    w.u32(rankOf(cache.playtimeRanks));
    w.u32(hyperHoldingOwnerRank(holdings, session.username));
    connection.send(w);
}

void GameServer::refreshLeaderboard(LeaderboardCache& cache, bool includeAdmins) {
    // Ranked over ACCOUNTS, not over the players currently online: the board is
    // a record of progress, and a top player who logged off has not lost it.
    struct Row {
        const std::string* name;
        double totalXp;
    };
    std::vector<Row> rows;
    rows.reserve(database_.userCount());
    // Every account, through the const lookup: the board only reads, and any
    // player may ask for it as often as they like. The mutable lookup drops
    // each row's cached text, so one request used to have the next save
    // re-serialise the whole users table on the tick thread (Database::Table).
    const Database& accounts = database_;
    const std::int64_t dayAgo = accounts.nowMillis() - 24 * 60 * 60 * 1000;
    cache.activeToday = 0;
    for (const std::string& username : accounts.usernames()) {
        const Account* account = accounts.findUser(username);
        if (account == nullptr) continue;
        if (account->lastActiveAtMillis >= dayAgo) ++cache.activeToday;
        // Staff are off the board unless the asker ticked "Show Admins on
        // Leaderboard". The browser's getLeaderboard(limit, includeAdmins)
        // honours that for any caller, not only an admin: it reveals names
        // and XP, which the board shows for everyone else anyway.
        if (account->admin && !includeAdmins) continue;
        const PlayerRecord* record = accounts.findProgress(account->id);
        rows.push_back({&account->username, record ? record->totalXp : 0.0});
    }

    // The browser client asks for limit=50 and the panel scrolls all fifty;
    // still one byte on the wire, so the count below stays a u8.
    constexpr std::size_t kRows = 50;
    const std::size_t shown = std::min(kRows, rows.size());
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.totalXp != b.totalXp ? a.totalXp > b.totalXp : *a.name < *b.name;
    });
    cache.levelRanks.clear();
    for (std::size_t i = 0; i < rows.size(); ++i)
        cache.levelRanks.emplace(*rows[i].name, static_cast<std::uint32_t>(i + 1));

    // The count beside the title is over every account, not over the 50 rows
    // sent. The active-today figure rides along only for an admin, which is
    // how the browser's payload leaves the field out for everyone else -- and 0
    // is the same answer as "absent" to the panel, since the asker is always
    // active today themselves.
    cache.users = static_cast<std::uint32_t>(accounts.userCount());
    cache.shown = static_cast<std::uint8_t>(shown);
    cache.rows.clear();
    for (std::size_t i = 0; i < shown; ++i) {
        cache.rows.str(*rows[i].name);
        cache.rows.u16(static_cast<std::uint16_t>(levelFromTotalXp(rows[i].totalXp).level));
        cache.rows.f64(rows[i].totalXp);
    }
    const auto playtimes = playtimeBoard(accounts, includeAdmins, accounts.userCount());
    const auto shownPlaytime = std::min(kRows, playtimes.size());
    cache.playtimeRanks.clear();
    cache.playtimes.clear();
    cache.playtimes.u8(static_cast<std::uint8_t>(shownPlaytime));
    for (std::size_t i = 0; i < playtimes.size(); ++i) {
        const auto& row = playtimes[i];
        cache.playtimeRanks.emplace(row.name, static_cast<std::uint32_t>(i + 1));
        if (i < shownPlaytime) {
            cache.playtimes.str(row.name);
            cache.playtimes.f64(row.seconds);
        }
    }
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

void GameServer::addNotification(const std::string& type, const std::string& message) {
    // rawArrayTable, never rawTable: this is the one unmodelled table stored
    // as an ARRAY, the shape the browser build gave it, and coercing it would
    // replace the whole feed with an empty object.
    Json& feed = database_.rawArrayTable("notifications");
    const std::int64_t now = database_.nowMillis();
    Json entry = Json::object();
    entry["id"] = std::to_string(now) + "-" + std::to_string(++notificationSequence_);
    entry["type"] = type;
    entry["message"] = message;
    entry["timestamp"] = static_cast<double>(now);
    feed.push(std::move(entry));

    // Trimmed from the FRONT, because the feed is in the order it was written
    // and the newest row is the one just pushed.
    std::vector<Json>& rows = feed.items();
    if (rows.size() > kNotificationHistory) {
        rows.erase(rows.begin(),
                   rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - kNotificationHistory));
    }
    database_.markDirty();
}

void GameServer::handleNotifications(net::Connection& connection, ByteReader& reader) {
    const std::uint16_t limit = reader.u16();
    const double before = reader.f64();
    if (!reader.ok()) return;

    // Read through storedTable(), never rawTable(): this is the one unmodelled
    // table stored as an ARRAY, the shape the browser build gave it, and
    // rawTable() would coerce the whole feed to an empty object on the way
    // past.
    const Json& table = database_.storedTable("notifications");

    struct Row {
        const Json* entry;
        double stamp;
    };
    std::vector<Row> rows;
    if (table.isArray()) {
        rows.reserve(table.size());
        for (const Json& entry : table.items()) {
            const double stamp = entry["timestamp"].asDouble();
            // `before` is exclusive, which is what lets a page request start
            // exactly at the oldest entry the client already holds.
            if (before > 0 && stamp >= before) continue;
            rows.push_back({&entry, stamp});
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.stamp > b.stamp; });

    const std::size_t want = std::min<std::size_t>(limit == 0 ? 50 : limit, 200);
    const std::size_t shown = std::min(want, rows.size());

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Notifications));
    // "More" is a full page, not a count of what is left: the browser reads
    // `batch.length === limit` and so pages one request past the end.
    w.boolean(shown == want);
    w.u16(static_cast<std::uint16_t>(shown));
    for (std::size_t i = 0; i < shown; ++i) {
        const Json& entry = *rows[i].entry;
        w.str(entry["id"].asString());
        w.u8(static_cast<std::uint8_t>(notificationKind(entry["type"].asString())));
        w.str(entry["message"].asString());
        w.f64(rows[i].stamp);
    }
    connection.send(w);
}

// ---------------------------------------------------------------------------
// Guilds
// ---------------------------------------------------------------------------

std::string GameServer::guildNameForUser(const std::string& username) const {
    if (username.empty()) return {};
    const Json& guilds = database_.storedTable("guilds");
    if (!guilds.isObject()) return {};
    for (const std::string& key : guilds.keys()) {
        if (guildMemberIndex(guilds[key], username) >= 0) return key;
    }
    return {};
}

Session* GameServer::sessionForUser(const std::string& username) {
    const std::string key = lowerCase(username);
    if (key.empty()) return nullptr;
    for (auto& [id, session] : sessions_) {
        if (session.authenticated() && lowerCase(session.username) == key) return &session;
    }
    return nullptr;
}

net::Connection* GameServer::connectionForUser(const std::string& username) {
    const Session* session = sessionForUser(username);
    return session ? listener_.find(session->connection) : nullptr;
}

void GameServer::sendChatTo(net::Connection& connection, net::ChatChannel channel,
                            const std::string& author, const std::string& text) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Chat));
    w.u8(static_cast<std::uint8_t>(channel));
    w.str(author);
    w.str(text);
    // Nobody in the world said this: a directed line is the server answering
    // one player, so it prints in the transcript and floats over no flower.
    w.u32(0);
    connection.send(w);
}

ByteWriter GameServer::guildRosterMessage(const Json& guild) {
    const Json& members = guild["memberUsernames"];

    // Everyone signed in, by lower-cased name, once per message rather than
    // once per member: sessionForUser is a walk of every session, and a full
    // guild asked it two hundred times.
    std::unordered_map<std::string, const Session*> signedIn;
    for (const auto& [id, session] : sessions_) {
        if (session.authenticated()) signedIn[lowerCase(session.username)] = &session;
    }

    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::GuildUpdate));
    w.boolean(true);
    w.str(guild["name"].asString());
    w.str(guildDisplayName(guild));
    w.str(guild["description"].asString());
    w.str(guild["leaderUsername"].asString());
    const auto deputies = guildDeputies(guild);
    w.u8(static_cast<std::uint8_t>(deputies.size()));
    for (const auto& deputy : deputies) w.str(deputy);
    w.u16(static_cast<std::uint16_t>(members.size()));
    for (std::size_t i = 0; i < members.size(); ++i) {
        const std::string member = members[i].asString();
        const auto found = signedIn.find(lowerCase(member));
        const Session* session = found != signedIn.end() ? found->second : nullptr;
        w.str(member);
        w.boolean(session != nullptr);
        w.str(session != nullptr ? guildLocation(*session) : std::string());
    }
    return w;
}

std::string GameServer::guildLocation(const Session& session) const {
    return biomeLabel(biomeOfEntity(session.entity));
}

void GameServer::serviceGuildPresence(double nowMillis) {
    // Once a second: a roster that names the biome a member is in is allowed
    // to be a second behind them, and a walk of every session every tick to
    // find out nobody moved is not worth having.
    constexpr double kIntervalMillis = 1000.0;
    if (nowMillis < nextGuildPresenceMillis_) return;
    nextGuildPresenceMillis_ = nowMillis + kIntervalMillis;

    std::unordered_map<std::string, std::string> now;
    for (const auto& [id, session] : sessions_) {
        if (!session.authenticated() || session.username.empty()) continue;
        now[lowerCase(session.username)] = guildLocation(session);
    }

    std::vector<std::string> moved;
    for (const auto& [name, location] : now) {
        const auto was = guildPresence_.find(name);
        if (was == guildPresence_.end() || was->second != location) moved.push_back(name);
    }
    for (const auto& [name, location] : guildPresence_) {
        if (now.find(name) == now.end()) moved.push_back(name);
    }
    guildPresence_ = std::move(now);
    if (moved.empty()) return;

    // One roster per guild however many of its members moved.
    std::vector<std::string> guildNames;
    for (const std::string& name : moved) {
        const std::string guild = guildNameForUser(name);
        if (guild.empty()) continue;
        if (std::find(guildNames.begin(), guildNames.end(), guild) == guildNames.end()) {
            guildNames.push_back(guild);
        }
    }
    const Json& guilds = database_.storedTable("guilds");
    for (const std::string& guild : guildNames) broadcastGuildRoster(guilds[guild]);
}

void GameServer::serviceOnlineRoster(double nowMillis) {
    // Same cadence spirit as guild presence: a second or two behind is fine,
    // and rebuilding every tick for an unchanged list is free work on the
    // hot path. Fingerprint short-circuits the broadcast.
    constexpr double kIntervalMillis = 2000.0;
    constexpr std::size_t kMaxRows = 2048;
    if (nowMillis < nextOnlineRosterMillis_) return;
    nextOnlineRosterMillis_ = nowMillis + kIntervalMillis;

    struct Row {
        std::string name, account;
        std::uint8_t shard=0;
        bool sameShard=true;
        std::uint16_t level = 1;
        std::string guild;
        std::string location;
        std::string sortKey;
        bool placed = false;
        std::uint8_t realm = 0;
        float x = 0.f;
        float y = 0.f;
        bool insideDungeon = false;
        std::uint8_t exteriorRealm = 0;
        float exteriorX=0, exteriorY=0, healthRatio=0;
    };
    std::vector<Row> rows;
    rows.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
        const Session& session = entry.second;
        // Mirror the crown Players tab's universe: signed-in accounts. Title-
        // screen sitters stay listed with an empty location (protocol).
        if (!session.authenticated() || session.username.empty()) continue;
        Row row;row.account=session.userId;
        row.name = session.displayName.empty() ? session.username : session.displayName;
        row.sortKey = lowerCase(session.username);
        row.location = guildLocation(session);
        if (const PlayerRecord* progress = database_.findProgress(session.userId)) {
            const int level = levelFromTotalXp(progress->totalXp).level;
            row.level = static_cast<std::uint16_t>(std::clamp(level, 1, 65535));
        }
        // World position for the whole-map dots: AOI never carries far-away
        // humans, so the roster is what paints them. Bots are not sessions.
        if (session.playing() && world_.isAlive(session.entity)) {
            if (const Transform* t = world_.tryGet<Transform>(session.entity)) {
                row.placed = true;
                row.realm = static_cast<std::uint8_t>(t->realm);
                row.x = static_cast<float>(t->position.x);
                row.y = static_cast<float>(t->position.y);
                if(const auto* health=world_.tryGet<Health>(session.entity))
                    row.healthRatio=health->max>0?static_cast<float>(clamp(health->current/health->max,0.0,1.0)):0;
                Realm exterior;Vec2 entrance;
                if(dungeons_ && dungeons_->exteriorForRealm(t->realm,exterior,entrance)) {
                    row.insideDungeon=true;row.exteriorRealm=static_cast<std::uint8_t>(exterior);
                    row.exteriorX=static_cast<float>(entrance.x);row.exteriorY=static_cast<float>(entrance.y);
                    row.location="Termite Mound";
                }
            }
        }
        rows.push_back(std::move(row));
    }
    if (!rows.empty()) {
        // Build once for this roster. Keep the first matching guild in save
        // order, as guildNameForUser does for an older duplicate membership.
        std::unordered_map<std::string, std::string_view> guildByMember;
        const auto& guilds = database_.storedTable("guilds");
        for (const auto& guild : guilds.keys()) {
            for (const auto& member : guilds[guild]["memberUsernames"].items()) {
                guildByMember.try_emplace(lowerCase(member.asString()), guild);
            }
        }
        for (auto& row : rows) {
            const auto found = guildByMember.find(row.sortKey);
            if (found != guildByMember.end()) row.guild = publicGuildTag(found->second);
        }
    }
    if(config_.authorityAuth) {
        Json input=Json::object();input["action"]="presence";input["rows"]=Json::array();
        for(const auto& row:rows) {
            Json item=Json::object();item["account"]=row.account;item["name"]=row.name;
            item["level"]=int(row.level);item["guild"]=row.guild;item["location"]=row.location;
            input["rows"].push(std::move(item));
        }
        const auto shared=config_.authorityAuth(input);const auto local=shared["current"].asString();
        for (const auto& notice : shared["notices"].items()) {
            if (notice["kind"].asString() == "chat") {
                const int channel = notice["channel"].asInt(-1);
                if (channel == 0 || channel == 1 || channel == 7)
                    broadcastChat(static_cast<net::ChatChannel>(channel), notice["author"].asString(),
                                  notice["text"].asString(), 0, false);
                continue;
            }
            const int mob = notice["mob"].asInt(-1);
            const int tier = notice["rarity"].asInt(-1);
            if (mob < 0 || mob >= kInvalidIndex || tier < 0 || tier >= kRarityCount) continue;
            announceMob(static_cast<std::uint16_t>(mob), static_cast<Rarity>(tier),
                        std::nullopt, notice["summoned"].asBool());
        }
        for(auto& row:rows)row.shard=local=="one"?1:local=="two"?2:0;
        for(const auto& item:shared["rows"].items()) {
            const auto shard=item["shard"].asString();if(shard==local||(shard!="one"&&shard!="two"))continue;
            Row row;row.account=item["account"].asString();
            if(std::any_of(rows.begin(),rows.end(),[&](const Row& r){return r.account==row.account;}))continue;
            row.name=item["name"].asString();row.guild=publicGuildTag(item["guild"].asString());row.location=item["location"].asString();
            row.level=static_cast<std::uint16_t>(std::clamp(item["level"].asInt(),1,65535));
            row.sortKey=lowerCase(row.name);row.shard=shard=="one"?1:2;row.sameShard=false;
            rows.push_back(std::move(row));
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
        return a.name < b.name;
    });
    if (rows.size() > kMaxRows) rows.resize(kMaxRows);

    std::string fingerprint;
    fingerprint.reserve(rows.size() * 48);
    for (const Row& row : rows) {
        fingerprint.append(std::to_string(row.shard)+(row.sameShard?"L":"R"));
        fingerprint.append(row.name);
        fingerprint.push_back('\n');
        fingerprint.append(std::to_string(row.level));
        fingerprint.push_back('\n');
        fingerprint.append(row.guild);
        fingerprint.push_back('\n');
        fingerprint.append(row.location);
        fingerprint.append(std::to_string(static_cast<int>(row.healthRatio*100)));
        if(row.insideDungeon)fingerprint.append("inside:"+std::to_string(row.exteriorRealm)+":"+std::to_string(row.exteriorX)+","+std::to_string(row.exteriorY));
        fingerprint.push_back('\n');
        // Coarse so a player pacing in place does not force a resend every
        // interval; ~32u steps still look right on a whole-map corner.
        if (row.placed) {
            fingerprint.append(std::to_string(row.realm));
            fingerprint.push_back(':');
            fingerprint.append(std::to_string(static_cast<int>(row.x) >> 5));
            fingerprint.push_back(',');
            fingerprint.append(std::to_string(static_cast<int>(row.y) >> 5));
            fingerprint.push_back('\n');
        } else {
            fingerprint.append("-\n");
        }
    }
    if (onlineRosterRevision_ == 0 || fingerprint != onlineRosterFingerprint_) {
        onlineRosterFingerprint_ = std::move(fingerprint);
        ByteWriter& w = onlineRosterMessage_;
        w.clear();
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::OnlineRoster));
        w.u16(static_cast<std::uint16_t>(rows.size()));
        for (const Row& row : rows) {
            w.str(row.name);
            w.u16(row.level);
            w.str(row.guild);
            w.str(row.location);
            w.u8(row.placed ? 1 : 0);
            if (row.placed) {
                w.u8(row.realm);
                w.f32(row.x);
                w.f32(row.y);
            }
            w.u8(row.insideDungeon?1:0);
            if(row.insideDungeon){w.u8(row.exteriorRealm);w.f32(row.exteriorX);w.f32(row.exteriorY);}
            w.f32(row.healthRatio);w.u8(row.shard);w.boolean(row.sameShard);
        }
        ++onlineRosterRevision_;
    }
}

void GameServer::sendDueOnlineRosters() {
    if (onlineRosterRevision_ == 0) return;
    // Every roster replaces the last one. Send just the latest to a viewer
    // once its connection can keep up, even if the roster then stops changing.
    for (auto& [id, session] : sessions_) {
        if (!session.authenticated() || session.onlineRosterRevision == onlineRosterRevision_) continue;
        auto* connection = listener_.find(id);
        if (!connection || connection->snapshotBacklog() >= kSnapshotBacklogLimit) continue;
        connection->send(onlineRosterMessage_);
        session.onlineRosterRevision = onlineRosterRevision_;
    }
}

void GameServer::sendNoGuild(net::Connection& connection) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::GuildUpdate));
    w.boolean(false);
    connection.send(w);
}

void GameServer::broadcastGuildRoster(const Json& guild) {
    // Addressed member by member -- the roster is only sent to people in the
    // guild, and there is no room concept here to address them as a group --
    // but built once, since it says the same thing to every one of them.
    const Json& members = guild["memberUsernames"];
    if (members.size() == 0) return;
    const ByteWriter message = guildRosterMessage(guild);
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (net::Connection* peer = connectionForUser(members[i].asString())) {
            peer->send(message);
        }
    }
}

void GameServer::sendGuildState(const Session& session, net::Connection& connection) {
    const std::string name = guildNameForUser(session.username);
    if (name.empty()) {
        sendNoGuild(connection);
        return;
    }
    // Every member is told, not just this one: somebody logging in changes the
    // online column of every roster that lists them.
    broadcastGuildRoster(database_.storedTable("guilds")[name]);
}

void GameServer::handleGuildCreate(Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const std::string tag = reader.str();
    const std::string displayName = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    guildCreate(session, connection, tag, displayName);
}

void GameServer::guildCreate(Session& session, net::Connection& connection,
                             const std::string& raw, const std::string& rawDisplayName) {
    if (!session.authenticated()) return;

    const std::string name = normalizeGuildName(raw);
    if (name.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Guild tag cannot be empty.");
        return;
    }
    if (!validGuildName(name)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Guild tag must contain only A-Z and 0-9.");
        return;
    }
    // An omitted display name falls back to the tag when shown.
    const std::string displayName = trimmed(rawDisplayName);
    if (!displayName.empty() && !validGuildDisplayName(displayName)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Guild name must be 1-" + std::to_string(kMaxGuildDisplayName) +
                       " letters, digits, spaces or punctuation.");
        return;
    }
    if (!guildNameForUser(session.username).empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are already in a guild.");
        return;
    }

    Json& guilds = database_.rawTable("guilds");
    if (guilds.contains(name)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "A guild named \"" + publicGuildTag(name) + "\" already exists.");
        return;
    }

    Json guild = Json::object();
    guild["name"] = name;
    guild["displayName"] = displayName;
    guild["description"] = std::string();
    guild["leaderUsername"] = session.username;
    Json members = Json::array();
    members.push(session.username);
    guild["memberUsernames"] = std::move(members);
    guild["createdAt"] = static_cast<double>(database_.nowMillis());
    guilds[name] = std::move(guild);
    database_.markDirty();

    sendNotice(connection, net::NoticeSeverity::Good,
               "Guild \"" + escapedMarkup(displayName.empty() ? publicGuildTag(name) : displayName) +
                   "\" [" + publicGuildTag(name) + "] created.");
    broadcastGuildRoster(guilds[name]);
}

void GameServer::handleGuildEdit(Session& session, net::Connection& connection,
                                 ByteReader& reader) {
    const std::string displayName = reader.str();
    const std::string description = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    guildEdit(session, connection, displayName, description);
}

void GameServer::guildEdit(Session& session, net::Connection& connection,
                           const std::optional<std::string>& rawDisplayName,
                           const std::optional<std::string>& rawDescription) {
    if (!session.authenticated()) return;

    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are not in a guild.");
        return;
    }
    Json& guild = database_.rawTable("guilds")[guildName];
    if (lowerCase(guild["leaderUsername"].asString()) != lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Only the guild leader can edit the guild.");
        return;
    }

    // Both checked before either is written, so a refusal changes nothing.
    std::string displayName;
    if (rawDisplayName) {
        displayName = trimmed(*rawDisplayName);
        if (!validGuildDisplayName(displayName)) {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "Guild name must be 1-" + std::to_string(kMaxGuildDisplayName) +
                           " letters, digits, spaces or punctuation.");
            return;
        }
    }
    std::string description;
    if (rawDescription) {
        description = trimmed(*rawDescription);
        if (!validGuildDescription(description)) {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "Guild description must be at most " +
                           std::to_string(kMaxGuildDescription) +
                           " letters, digits, spaces or punctuation.");
            return;
        }
    }

    const Json& stored = guild;
    const bool renamed = rawDisplayName && displayName != guildDisplayName(stored);
    const bool redescribed = rawDescription && description != stored["description"].asString();
    if (!renamed && !redescribed) return;
    if (renamed) guild["displayName"] = displayName;
    if (redescribed) guild["description"] = description;
    database_.markDirty();

    sendNotice(connection, net::NoticeSeverity::Good, "Guild updated.");
    broadcastGuildRoster(guild);
}

void GameServer::handleGuildInvite(Session& session, net::Connection& connection,
                                   ByteReader& reader) {
    const std::string raw = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    guildInvite(session, connection, raw);
}

void GameServer::guildInvite(Session& session, net::Connection& connection,
                             const std::string& raw) {
    if (!session.authenticated()) return;

    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are not in a guild.");
        return;
    }
    const Json& guild = database_.storedTable("guilds")[guildName];
    const std::string inviter = lowerCase(session.username);
    const auto deputies = guildDeputies(guild);
    if (lowerCase(guild["leaderUsername"].asString()) != inviter &&
        std::none_of(deputies.begin(), deputies.end(), [&](const auto& name) {
            return lowerCase(name) == inviter;
        })) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Only the guild leader or deputy can invite players.");
        return;
    }
    if (guild["memberUsernames"].size() >= kMaxGuildSize) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Guild is full (max " + std::to_string(kMaxGuildSize) + " members).");
        return;
    }

    const std::string target = trimmed(raw);
    if (target.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Please provide a username to invite.");
        return;
    }
    if (database_.findUser(target) == nullptr) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "No player named \"" + target + "\" exists.");
        return;
    }
    if (lowerCase(target) == lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You cannot invite yourself.");
        return;
    }
    if (!guildNameForUser(target).empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, target + " is already in a guild.");
        return;
    }

    const std::string key = lowerCase(target);
    const std::int64_t now = database_.nowMillis();
    const auto existing = guildInvites_.find(key);
    if (existing != guildInvites_.end() && existing->second.expiresAtMillis > now) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   target + " already has a pending guild invite.");
        return;
    }

    guildInvites_[key] = {guildName, now + kGuildInviteMillis};
    sendNotice(connection, net::NoticeSeverity::Good, "Guild invite sent to " + target + ".");

    if (net::Connection* peer = connectionForUser(target)) {
        ByteWriter w;
        w.u8(static_cast<std::uint8_t>(net::ServerMessage::GuildInviteReceived));
        w.str(guildName);
        w.str(session.username);
        w.str(guildDisplayName(guild));
        peer->send(w);
        sendNotice(*peer, net::NoticeSeverity::Info,
                   "@" + session.username + " has invited you to guild \"" +
                       escapedMarkup(guildDisplayName(guild)) + "\" [" + guildName +
                       "]. Use /guild-accept or /guild-decline.");
    }
}

void GameServer::handleGuildAccept(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    const std::string key = lowerCase(session.username);
    const auto invite = guildInvites_.find(key);
    if (invite == guildInvites_.end()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You have no pending guild invite.");
        return;
    }
    // Every failure below drops the invitation: an invite that cannot be taken
    // up is spent, or a player refused by a full guild would keep a banner they
    // can never clear.
    const std::string guildName = invite->second.guildName;
    if (invite->second.expiresAtMillis < database_.nowMillis()) {
        guildInvites_.erase(invite);
        sendNotice(connection, net::NoticeSeverity::Warning, "Guild invite has expired.");
        return;
    }

    Json& guilds = database_.rawTable("guilds");
    if (!guilds.contains(guildName)) {
        guildInvites_.erase(invite);
        sendNotice(connection, net::NoticeSeverity::Warning, "Guild no longer exists.");
        return;
    }
    Json& guild = guilds[guildName];
    if (guild["memberUsernames"].size() >= kMaxGuildSize) {
        guildInvites_.erase(invite);
        sendNotice(connection, net::NoticeSeverity::Warning, "Guild is full.");
        return;
    }
    if (!guildNameForUser(session.username).empty()) {
        guildInvites_.erase(invite);
        sendNotice(connection, net::NoticeSeverity::Warning, "You are already in a guild.");
        return;
    }

    guild["memberUsernames"].push(session.username);
    guildInvites_.erase(invite);
    database_.markDirty();

    sendGuildSystem(guild, session.username + " has joined the guild.");
    broadcastGuildRoster(guild);
}

void GameServer::handleGuildDecline(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    guildInvites_.erase(lowerCase(session.username));
    sendNotice(connection, net::NoticeSeverity::Info, "Guild invite declined.");
}

void GameServer::handleGuildLeave(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are not in a guild.");
        return;
    }

    Json& guilds = database_.rawTable("guilds");
    Json& guild = guilds[guildName];
    const int at = guildMemberIndex(guild, session.username);
    if (at >= 0) {
        Json& members = guild["memberUsernames"];
        members.items().erase(members.items().begin() + at);
    }

    // The client is told it has no guild before anything else, so the panel
    // flips to the no-guild view on the same frame the confirmation lands.
    sendNoGuild(connection);
    sendNotice(connection, net::NoticeSeverity::Info, "You have left the guild.");

    if (guild["memberUsernames"].size() == 0) {
        // The last member out disbands it rather than leaving an empty guild
        // holding a five-character name nobody else can claim.
        guilds.erase(guildName);
        database_.markDirty();
        return;
    }

    std::string promoted;
    if (lowerCase(guild["leaderUsername"].asString()) == lowerCase(session.username)) {
        promoted = guild["memberUsernames"][std::size_t{0}].asString();
        guild["leaderUsername"] = promoted;
    }
    setGuildDeputies(guild, guildDeputies(guild));
    database_.markDirty();

    sendGuildSystem(guild, session.username + " has left the guild.");
    if (!promoted.empty()) sendGuildSystem(guild, promoted + " is now the guild leader.");
    broadcastGuildRoster(guild);
}

void GameServer::handleGuildKick(Session& session, net::Connection& connection,
                                 ByteReader& reader) {
    const std::string raw = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    guildKick(session, connection, raw);
}

void GameServer::guildTransfer(Session& session, net::Connection& connection,
                               const std::string& raw) {
    if (!session.authenticated()) return;
    const auto guildName = guildNameForUser(session.username);
    if (guildName.empty()) return;
    Json& guild = database_.rawTable("guilds")[guildName];
    if (lowerCase(guild["leaderUsername"].asString()) != lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "只有现任会长可以转让公会。");
        return;
    }
    const int at = guildMemberIndex(guild, trimmed(raw));
    if (at < 0 || lowerCase(trimmed(raw)) == lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "请选择本公会的其他成员。");
        return;
    }
    const auto member = guild["memberUsernames"][static_cast<std::size_t>(at)].asString();
    guild["leaderUsername"] = member;
    setGuildDeputies(guild, guildDeputies(guild));
    database_.markDirty();
    sendGuildSystem(guild, session.username + " 已将会长转让给 " + member + "。");
    broadcastGuildRoster(guild);
}

void GameServer::guildDeputy(Session& session, net::Connection& connection,
                             const std::string& raw, bool remove) {
    if (!session.authenticated()) return;
    const auto guildName = guildNameForUser(session.username);
    if (guildName.empty()) return;
    Json& guild = database_.rawTable("guilds")[guildName];
    if (lowerCase(guild["leaderUsername"].asString()) != lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "只有现任会长可以设置副会长。");
        return;
    }
    auto deputies = guildDeputies(guild);
    std::string target = trimmed(raw);
    if (target.empty()) {
        if (remove || deputies.size() != 1) {
            sendNotice(connection, net::NoticeSeverity::Info, "使用 /guild-deputy 名称任命，/guild-undeputy 名称撤销。");
            return;
        }
        target = deputies.front();
        remove = true;
    }
    const int index = guildMemberIndex(guild, target);
    if (index < 0 || lowerCase(target) == lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning, "请选择本公会的其他成员。");
        return;
    }
    const auto member = guild["memberUsernames"][static_cast<std::size_t>(index)].asString();
    const auto appointed = std::find(deputies.begin(), deputies.end(), member);
    if (remove) {
        if (appointed == deputies.end()) return;
        deputies.erase(appointed);
        sendGuildSystem(guild, member + " 的副会长职位已撤销。");
    } else {
        if (appointed != deputies.end()) return;
        if (deputies.size() >= kMaxGuildDeputies) {
            sendNotice(connection, net::NoticeSeverity::Warning,
                       "副会长名额已满，最多 " + std::to_string(kMaxGuildDeputies) + " 人。");
            return;
        }
        deputies.push_back(member);
        sendGuildSystem(guild, member + " 已成为副会长，可以邀请新成员。");
    }
    setGuildDeputies(guild, deputies);
    database_.markDirty();
    broadcastGuildRoster(guild);
}

void GameServer::guildKick(Session& session, net::Connection& connection,
                           const std::string& raw) {
    if (!session.authenticated()) return;

    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are not in a guild.");
        return;
    }
    Json& guild = database_.rawTable("guilds")[guildName];
    if (lowerCase(guild["leaderUsername"].asString()) != lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "Only the guild leader can kick players.");
        return;
    }
    const std::string target = trimmed(raw);
    if (lowerCase(target) == lowerCase(session.username)) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "You cannot kick yourself. Use /guild-leave instead.");
        return;
    }
    const int at = guildMemberIndex(guild, target);
    if (at < 0) {
        sendNotice(connection, net::NoticeSeverity::Warning, target + " is not in your guild.");
        return;
    }

    // The stored spelling, not what the leader typed: the kicked player's own
    // messages read back the name their account carries.
    const std::string member = guild["memberUsernames"][static_cast<std::size_t>(at)].asString();
    Json& members = guild["memberUsernames"];
    members.items().erase(members.items().begin() + at);
    setGuildDeputies(guild, guildDeputies(guild));
    database_.markDirty();

    sendGuildSystem(guild, member + " was kicked from the guild by " + session.username + ".");
    if (net::Connection* peer = connectionForUser(member)) {
        sendNoGuild(*peer);
        sendNotice(*peer, net::NoticeSeverity::Bad,
                   "You were kicked from guild \"" + guildName + "\".");
    }
    broadcastGuildRoster(guild);
}

// ---------------------------------------------------------------------------
// Scheduled restart
// ---------------------------------------------------------------------------

namespace {

/// How long before a restart each warning goes out, longest first. The
/// reference's RESTART_WARNINGS_MS.
constexpr std::array<double, 4> kRestartWarnings = {600000.0, 300000.0, 60000.0, 10000.0};

/// "daily" is the only reason with a friendlier name than itself.
std::string restartReasonText(const std::string& reason) {
    return reason == "daily" ? "daily maintenance" : reason;
}

/// The reference prefixes both of these with U+26A0. It is dropped here for
/// the same reason /guild-info's bullet was: there is one font in this build
/// and it cannot draw that glyph, so the sign would arrive as a hole in the
/// sentence. The words are the reference's, unchanged.
std::string restartWarning(double millis, const std::string& reason) {
    if (millis >= 60000.0) {
        const long minutes = std::lround(millis / 60000.0);
        return "<span style=\"color:#ffb74d;\">Server will restart in " +
               std::to_string(minutes) + (minutes == 1 ? " minute (" : " minutes (") +
               restartReasonText(reason) + ").</span>";
    }
    const long seconds = std::lround(millis / 1000.0);
    return "<span style=\"color:#ff6b6b;\">Server restarting in " + std::to_string(seconds) +
           (seconds == 1 ? " second!</span>" : " seconds!</span>");
}

} // namespace

bool GameServer::scheduleRestart(double delayMillis, const std::string& reason) {
    // A restart that has already announced itself and closed the door is not
    // one anybody can reschedule.
    if (restart_.firing) return false;
    if (delayMillis < 0) delayMillis = 0;

    restart_.pending = true;
    restart_.atMillis = wallClockMillis_ + delayMillis;
    restart_.reason = reason;
    // Warnings longer than the delay itself are skipped outright rather than
    // fired late: "restarting in 10 minutes" said one second before the exit
    // is worse than saying nothing.
    restart_.warningsSaid = 0;
    while (restart_.warningsSaid < kRestartWarnings.size() &&
           kRestartWarnings[restart_.warningsSaid] >= delayMillis) {
        ++restart_.warningsSaid;
    }
    return true;
}

bool GameServer::cancelScheduledRestart() {
    if (restart_.firing || !restart_.pending) return false;
    restart_ = ScheduledRestart{};
    return true;
}

bool GameServer::scheduledRestartInfo(double& remainingMillis, std::string& reason) const {
    if (!restart_.pending && !restart_.firing) return false;
    remainingMillis = std::max(0.0, restart_.atMillis - wallClockMillis_);
    reason = restart_.reason;
    return true;
}

void GameServer::serviceScheduledRestart(double nowMillis) {
    if (restart_.firing) {
        // The gap between the last word and the exit exists so the socket
        // writes actually leave: stopping in the same breath as the broadcast
        // drops the message that explains the disconnect.
        //
        // Non-zero, and set before the stop so whoever reads it after the loop
        // cannot see a half-finished answer: a supervisor that is told the
        // server exited cleanly leaves it down, and a restart nobody restarts
        // is just a shutdown with a countdown. See exitCode().
        if (nowMillis >= restart_.stopAtMillis) {
            exitCode_ = kRestartExit;
            stop();
        }
        return;
    }
    if (!restart_.pending) return;

    const double remaining = restart_.atMillis - nowMillis;
    while (restart_.warningsSaid < kRestartWarnings.size() &&
           remaining <= kRestartWarnings[restart_.warningsSaid]) {
        broadcastChat(net::ChatChannel::System, "System",
                      restartWarning(kRestartWarnings[restart_.warningsSaid], restart_.reason));
        ++restart_.warningsSaid;
    }
    if (remaining > 0) return;

    restart_.pending = false;
    restart_.firing = true;
    restart_.stopAtMillis = nowMillis + 1000.0;
    broadcastChat(net::ChatChannel::System, "System",
                  "<span style=\"color:#ff6b6b;\">Server restarting now (" +
                      restartReasonText(restart_.reason) +
                      "). Reconnecting shortly...</span>");
    std::printf("[restart] scheduled restart triggered (reason: %s)\n", restart_.reason.c_str());
}

// ---------------------------------------------------------------------------
// Self-update
// ---------------------------------------------------------------------------

void GameServer::serviceAutoUpdate() {
    net::Connection* requester =
        updateRequester_ != 0 ? listener_.find(updateRequester_) : nullptr;

    // Drained whether or not anybody is still listening: the lines are also
    // the JS side's console output, and leaving them queued would replay a
    // finished update's whole log at the next admin who asks for one.
    for (const std::string& line : autoupdate::drainLog()) {
        if (requester != nullptr) sendSystem(*requester, line);
    }

    switch (autoupdate::takeOutcome()) {
        case autoupdate::Outcome::Installed: {
            const bool scheduled = scheduleRestart(updateRestartDelayMillis_, "update");
            if (requester == nullptr) break;
            if (scheduled) {
                const long seconds = std::lround(updateRestartDelayMillis_ / 1000.0);
                sendSystem(*requester,
                           "[UPDATE] Done. Server restarts in " + std::to_string(seconds) +
                               "s to load the new build (\"restart cancel\" or \"update cancel\" "
                               "to abort the restart -- the new files stay installed either "
                               "way).");
            } else {
                sendSystem(*requester,
                           "[UPDATE] Done. A restart is already firing -- the new build loads "
                           "when the server comes back up.");
            }
            break;
        }
        case autoupdate::Outcome::Failed:
            // The JS side already said what went wrong, through the log above.
            break;
        case autoupdate::Outcome::None:
            break;
    }

    if (!autoupdate::inProgress()) updateRequester_ = 0;
}

// ---------------------------------------------------------------------------
// The maze
// ---------------------------------------------------------------------------

std::string GameServer::adminChangeMaze(const std::string& argument) {
    static const std::array<const char*, 3> kBiomeNames = {{"garden", "desert", "ocean"}};
    const auto biomeName = [](MazeBiome biome) {
        return kBiomeNames[static_cast<std::size_t>(biome)];
    };

    const std::int64_t currentDay = activeMaze().day();
    const std::string token = lowerCase(trimmed(argument));

    std::int64_t targetDay = 0;
    if (token.empty() || token == "next") {
        targetDay = currentDay + 1;
    } else if (token == "garden" || token == "desert" || token == "ocean") {
        // The three layouts are authored, not generated: the day number only
        // picks which one is active, so asking for a biome means asking for the
        // nearest day that lands on it.
        const auto want = static_cast<std::int64_t>(
            token == "garden" ? 0 : (token == "desert" ? 1 : 2));
        const std::int64_t current = ((currentDay % 3) + 3) % 3;
        const std::int64_t advance = ((want - current) + 3) % 3;
        if (advance == 0) return "Maze is already " + token + ".";
        targetDay = currentDay + advance;
    } else {
        int parsed = 0;
        bool numeric = !token.empty();
        std::size_t at = (token[0] == '-') ? 1 : 0;
        if (at >= token.size()) numeric = false;
        for (std::size_t i = at; numeric && i < token.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(token[i]))) numeric = false;
        }
        if (!numeric) {
            return std::string("Usage: change-maze [next|garden|desert|ocean|<dayNumber>] ")
                       .append("\xE2\x80\x94 current: day ") +
                   std::to_string(currentDay) + " (" + biomeName(activeMaze().biome()) + ")";
        }
        parsed = std::atoi(token.c_str());
        targetDay = parsed;
    }

    if (targetDay == currentDay) {
        return "Maze is already day " + std::to_string(currentDay) + " (" +
               biomeName(activeMaze().biome()) + ").";
    }

    mazeDayOffset_ = targetDay - currentMazeDay();
    setActiveMazeDay(targetDay);

    // Anyone standing in the old layout is standing in the new one's walls.
    // The reference moves them to the new entrance; so does this, and the
    // client cuts its interpolation on the jump by itself. Yesterday's mobs
    // would be inside the walls too, so they go (clearMazeEnemies), and every
    // client is told which maze to build now.
    const Vec2 entrance = activeMaze().spawn();
    Query<PlayerTag, Transform> flowers{world_};
    std::vector<Entity> inside;
    flowers.each([&](Entity entity, PlayerTag&, Transform& transform) {
        if (transform.realm == Realm::Maze) inside.push_back(entity);
    });
    for (const Entity entity : inside) teleportEntity(entity, entrance);
    modes_->clearMaze(world_, commands_);
    broadcastMazeInfo();

    return "Maze changed to day " + std::to_string(activeMaze().day()) + " (" +
           biomeName(activeMaze().biome()) + "). Offset from real day: " +
           (mazeDayOffset_ >= 0 ? "+" : "") + std::to_string(mazeDayOffset_) + ".";
}

// ---------------------------------------------------------------------------
// Squads
// ---------------------------------------------------------------------------
//
// The rules are server/squads.h. What lives here is everything those rules
// deliberately know nothing about: which body a member currently owns, what to
// call them, and who to tell.

std::string GameServer::squadAccountName(SquadMemberId member) {
    if (!member.bot()) {
        const Session* session = sessionFor(member.connection);
        return session != nullptr && !session->username.empty() ? session->username : "Unknown";
    }
    // A bot has no account. The reference reads its "username" off its
    // nameplate for exactly this listing, and `/squad-invite` matches on the
    // same string, so the two agree by construction.
    if (const PlayerAccount* account = world_.tryGet<PlayerAccount>(member.entity)) {
        return account->username;
    }
    return "Unknown";
}

std::string GameServer::squadDisplayName(SquadMemberId member) {
    if (!member.bot()) {
        const Session* session = sessionFor(member.connection);
        if (session == nullptr) return "Unknown";
        return session->displayName.empty() ? session->username : session->displayName;
    }
    return squadAccountName(member);
}

Entity GameServer::squadEntityOf(SquadMemberId member) const {
    if (member.bot()) {
        return world_.isAlive(member.entity) ? member.entity : NULL_ENTITY;
    }
    const auto it = sessions_.find(member.connection);
    if (it == sessions_.end() || !it->second.playing()) return NULL_ENTITY;
    return it->second.entity;
}

net::Connection* GameServer::squadConnection(SquadMemberId member) {
    return member.bot() ? nullptr : listener_.find(member.connection);
}

void GameServer::sendSquadUpdate(net::Connection& connection, Squad* squad) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::SquadUpdate));
    if (squad == nullptr) {
        w.boolean(false);
        connection.send(w);
        return;
    }
    w.boolean(true);
    w.str(squad->id);
    w.str(squads_.issueCode(*squad, rng_));
    w.boolean(squad->isPublic);

    // A ROW PER BODY, not per member. A splitter's owner is one member with
    // two flowers, and the party HUD is a list of bars over bodies -- a member
    // that showed only the half being steered would leave the other one, the
    // one you cannot see because you are not looking through it, with no
    // health bar anywhere on the screen.
    std::vector<std::pair<SquadMemberId, Entity>> rows;
    rows.reserve(squad->members.size() + 1);
    for (const SquadMemberId& member : squad->members) {
        rows.emplace_back(member, squadEntityOf(member));
        if (member.bot()) continue;
        const auto found = sessions_.find(member.connection);
        if (found != sessions_.end() && found->second.split()) {
            rows.emplace_back(member, found->second.splitOther);
        }
    }

    w.u8(static_cast<std::uint8_t>(rows.size()));
    for (const auto& row : rows) {
        w.str(squadAccountName(row.first));
        w.str(squadDisplayName(row.first));
        // The wire id, not the entity: it is what the client's own world is
        // keyed by, and it is 0 for a member with no body just now -- somebody
        // sitting on the title screen, or waiting on a death card.
        const NetId* id = row.second != NULL_ENTITY ? world_.tryGet<NetId>(row.second) : nullptr;
        w.u32(id != nullptr ? id->value : 0);
        std::uint8_t flags = 0;
        // The leader mark rides the member's OWN row, which is the first one
        // it has: a flower does not lead a squad twice for being two flowers.
        if (squad->leader == row.first && row.second == squadEntityOf(row.first)) flags |= 1u;
        if (row.first.bot()) flags |= 2u;
        w.u8(flags);
    }
    connection.send(w);
}

void GameServer::broadcastSquadUpdate(Squad& squad) {
    for (const SquadMemberId& member : squad.members) {
        if (net::Connection* peer = squadConnection(member)) sendSquadUpdate(*peer, &squad);
    }
}

void GameServer::sendSquadSystem(const Squad& squad, const std::string& text) {
    // On the Squad channel rather than System, which is what tells a member
    // whether a line was said to the world or to the four of them. No author:
    // it is the server's notice, and the client colours it as the squad's.
    for (const SquadMemberId& member : squad.members) {
        if (net::Connection* peer = squadConnection(member)) {
            sendChatTo(*peer, net::ChatChannel::Squad, "", text);
        }
    }
}

void GameServer::sendGuildSystem(const Json& guild, const std::string& text) {
    // The squad's notice, for a guild: on its own channel, with no author.
    const Json& members = guild["memberUsernames"];
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (net::Connection* peer = connectionForUser(members[i].asString())) {
            sendChatTo(*peer, net::ChatChannel::Guild, "", text);
        }
    }
}

bool GameServer::resolveSquadTarget(const std::string& name, SquadMemberId& out) {
    if (Session* session = sessionForUser(name)) {
        out = squadIdOf(*session);
        return true;
    }
    // Then a bot, by nameplate: bots own no account, and inviting one is half
    // of what squads are for on a quiet server.
    const std::string key = lowerCase(trimmed(name));
    if (key.empty()) return false;
    for (const Bot& bot : bots_) {
        if (bot.entity == NULL_ENTITY || !world_.isAlive(bot.entity)) continue;
        if (lowerCase(bot.name) != key) continue;
        out = SquadMemberId::ofBot(bot.entity);
        return true;
    }
    return false;
}

void GameServer::departSquad(Session& session, net::Connection* connection,
                             const std::string& leaverName) {
    const SquadRoster::Departure departure = squads_.leave(squadIdOf(session));
    if (!departure.wasMember) return;
    if (connection != nullptr) sendSquadUpdate(*connection, nullptr);

    Squad* remaining = squads_.find(departure.squadId);
    if (remaining == nullptr) return;
    // Promotion first, then the departure: that is the order the reference
    // emits them in, because the promotion happens inside its leaveSquad and
    // the "has left" line is said by the caller afterwards.
    if (departure.promoted.valid()) {
        sendSquadSystem(*remaining, squadDisplayName(departure.promoted) +
                                        " is now the squad leader.");
    }
    sendSquadSystem(*remaining, leaverName + " has left the squad.");
    broadcastSquadUpdate(*remaining);
}

void GameServer::removeBotFromSquad(Entity body) {
    const SquadMemberId member = SquadMemberId::ofBot(body);
    if (squads_.forMember(member) == nullptr) return;
    // Read while the body is still alive: a destroyed bot has no nameplate to
    // put in the line its squad is about to be sent.
    const std::string name = squadDisplayName(member);
    const SquadRoster::Departure departure = squads_.leave(member);
    Squad* remaining = squads_.find(departure.squadId);
    if (remaining == nullptr) return;
    if (departure.promoted.valid()) {
        sendSquadSystem(*remaining, squadDisplayName(departure.promoted) +
                                        " is now the squad leader.");
    }
    sendSquadSystem(*remaining, name + " has left the squad.");
    broadcastSquadUpdate(*remaining);
}

// A squad is one party working ONE biome.
//
// The rules in server/squads.h know nothing about where anybody is standing,
// and should not: they are the membership rules, they are pure, and they are
// tested without a world. Where a member IS, though, is the one thing a squad
// must agree about -- the party HUD, the pink minimap dots and the shared loot
// ranking all describe people you are playing WITH, and a squadmate three
// biomes away is none of those things. So the biome test lives here, beside
// the bodies, and it is applied at every door into a squad and again whenever
// somebody arrives in a new biome.

std::string GameServer::squadMemberBiome(SquadMemberId member) const {
    if (member.bot()) return biomeOfEntity(member.entity);
    const auto it = sessions_.find(member.connection);
    if (it == sessions_.end()) return {};
    const Session& session = it->second;
    // A live body first, corpse included: a player on a death card is still
    // standing where they fell, and their squad is still the one there.
    if (session.entity != NULL_ENTITY && world_.isAlive(session.entity)) {
        const std::string biome = biomeOfEntity(session.entity);
        if (!biome.empty()) return biome;
    }
    // Nobody home: where their spawn choice is about to put them, which is
    // what the squad will have to live with the moment they press play.
    //
    // Only a choice they actually MADE, though. spawnRealmFor() answers the
    // overworld for a session that has picked nothing -- that is the door the
    // picker opens on, not a place the player has said they are going -- and
    // taking it at its word made every lobby a garden lobby: a flower in the
    // desert could invite nobody who was not already standing in the world,
    // while one in the garden could invite everybody, because everybody with
    // no body was "in the garden". Unknown is unknown, and unknown is no
    // objection; the arrival is caught by enforceSquadBiome when they press
    // play.
    if (session.spawnChoice.empty()) return {};
    return biomeOfRealm(spawnRealmFor(session));
}

std::string GameServer::squadBiome(const Squad& squad) const {
    // A member with a BODY first, the leader's if they have one: where the
    // squad actually is beats where somebody sitting on the title screen has
    // said they intend to go. Without that order a leader who backed out to
    // the picker and clicked a different door would redefine the squad's
    // biome from the menu and lock out the members still playing in it.
    const std::string leaders = biomeOfEntity(squadEntityOf(squad.leader));
    if (!leaders.empty()) return leaders;
    for (const SquadMemberId& member : squad.members) {
        const std::string biome = biomeOfEntity(squadEntityOf(member));
        if (!biome.empty()) return biome;
    }
    // Nobody is in the world: fall back to what they have chosen, so a party
    // formed on the title screen still cannot form across two doors.
    for (const SquadMemberId& member : squad.members) {
        const std::string biome = squadMemberBiome(member);
        if (!biome.empty()) return biome;
    }
    return {};
}

bool GameServer::squadAcceptsBiome(const Squad& squad, SquadMemberId who) const {
    const std::string theirs = squadMemberBiome(who);
    const std::string ours = squadBiome(squad);
    // An unknown biome is not a biome of its own: a member the world cannot
    // place yet is no reason to refuse a party that is otherwise legal.
    return theirs.empty() || ours.empty() || theirs == ours;
}

std::string GameServer::squadBiomeRefusal(const Squad& squad, SquadMemberId who,
                                          const std::string& whoLabel) const {
    if (squadAcceptsBiome(squad, who)) return {};
    return whoLabel + " in " + biomeLabel(squadMemberBiome(who)) + ", and the squad is in " +
           biomeLabel(squadBiome(squad)) + ". A squad cannot span biomes.";
}

void GameServer::enforceSquadBiome(SquadMemberId member) {
    if(!member.bot())if(const auto* session=sessionFor(member.connection);session && session->split())return;
    Squad* squad = squads_.forMember(member);
    if (squad == nullptr) return;
    // Against the REST of the squad, not against squadBiome(): a leader who
    // walked out would otherwise redefine the squad's biome and throw
    // everybody else out of their own party instead of leaving it themselves.
    // BODIES only, on both sides. A member with no body has not gone
    // anywhere: they are on the title screen or on a death card, their door
    // is an intention rather than a place, and throwing somebody out of a
    // squad because a squadmate is browsing the picker is a bug the join-time
    // checks would never make.
    const std::string theirs = biomeOfEntity(squadEntityOf(member));
    if (theirs.empty()) return;
    std::string elsewhere;
    for (const SquadMemberId& other : squad->members) {
        if (other == member) continue;
        const std::string biome = biomeOfEntity(squadEntityOf(other));
        if (biome.empty()) continue;
        if (biome == theirs) return;   // somebody is still here: this is not the one who left
        if (elsewhere.empty()) elsewhere = biome;
    }
    // Read before the departure: leaving can disband the squad outright, and
    // the line telling somebody what they just left cannot be read off it
    // afterwards.
    if (elsewhere.empty()) return;

    const std::string name = squadDisplayName(member);
    if (member.bot()) {
        removeBotFromSquad(member.entity);
        return;
    }
    Session* session = sessionFor(member.connection);
    if (session == nullptr) return;
    net::Connection* connection = listener_.find(member.connection);
    departSquad(*session, connection, name);
    if (connection != nullptr) {
        sendNotice(*connection, net::NoticeSeverity::Warning,
                   "You left your squad by leaving " + biomeLabel(elsewhere) +
                       ". A squad cannot span biomes.");
    }
}

void GameServer::rebuildSquadIndex() {
    squadIndex_.clear();
    if (squads_.empty()) return;
    for (const auto& entry : squads_.all()) {
        std::vector<SquadBody> bodies;
        for (const SquadMemberId& member : entry.second.members) {
            const Entity body = squadEntityOf(member);
            if (body == NULL_ENTITY) continue;
            // A split member brings BOTH its flowers. They are one person --
            // contenderSize() in loot_eligibility.h counts them as one and
            // pays them one share -- and filing only the steered one would
            // leave the parked half ranking against its own owner as a
            // stranger, competing for the slots the corpse has.
            if (!member.bot()) {
                const auto found = sessions_.find(member.connection);
                if (found != sessions_.end() && found->second.split()) {
                    const PlayerAccount* other =
                        world_.tryGet<PlayerAccount>(found->second.splitOther);
                    bodies.push_back(SquadBody{found->second.splitOther,
                                               other != nullptr ? other->connection : 0});
                }
            }
            // BOTS INCLUDED. A bot is a member like any other here: its
            // damage pools into the squad's score and it counts in the
            // average, so a player squadded with bots is paid for what the
            // squad killed. What a bot does NOT get is a free share -- it
            // owns no account, so SquadBody::banks() is false and
            // loot_eligibility.h hands it nothing it did not earn. Leaving
            // bots out of the index entirely was worse in both directions:
            // it made a human-plus-bots squad rank as a solo player, which
            // on a quiet server is every squad there is.
            const PlayerAccount* account = world_.tryGet<PlayerAccount>(body);
            bodies.push_back(SquadBody{body, account != nullptr ? account->connection : 0});
        }
        // One body in the world is not a pool: leaving it out keeps the
        // ordinary case -- a squad whose other members are on the title screen
        // -- ranking exactly as a solo player does.
        if (bodies.size() < 2) continue;
        const std::size_t group = squadIndex_.groups.size();
        for (const SquadBody& member : bodies) squadIndex_.group[member.body] = group;
        squadIndex_.groups.push_back(std::move(bodies));
    }
}

Squad* GameServer::squadOrCreate(Session& session, net::Connection& connection) {
    const SquadMemberId me = squadIdOf(session);
    if (Squad* existing = squads_.forMember(me)) return existing;
    Squad* squad = squads_.create(me, false, rng_);
    // A squad made this way is announced nowhere else, so the client that
    // caused it would otherwise not know it exists.
    if (squad != nullptr) sendSquadUpdate(connection, squad);
    return squad;
}

void GameServer::collectSquadBodies(const Session& session, std::vector<Entity>& out) {
    out.clear();
    // A split flower's own other half, first and whatever else is true: it is
    // the one body on the map this client must never lose sight of, because
    // switching to it is one click away and a half that had scrolled off the
    // viewport would be switched into blind.
    if (session.split()) out.push_back(session.splitOther);
    const SquadMemberId me = squadIdOf(session);
    const Squad* squad = squads_.forMember(me);
    if (squad == nullptr) return;
    for (const SquadMemberId& member : squad->members) {
        if (member == me) continue;
        const Entity body = squadEntityOf(member);
        if (body != NULL_ENTITY) out.push_back(body);
    }
}

void GameServer::handleGuildSquadAll(Session& session, net::Connection& connection) {
    if (!session.authenticated()) return;
    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, "You are not in a guild.");
        return;
    }
    // Before squadOrCreate, which would otherwise mint a squad for a flower
    // that is about to be told it may not have one.
    const std::string whileSplit = splitSquadRefusal(squadIdOf(session), "You are");
    if (!whileSplit.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, whileSplit);
        return;
    }
    Squad* squad = squadOrCreate(session, connection);
    if (squad == nullptr) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Failed to create a squad.");
        return;
    }

    const Json& members = database_.storedTable("guilds")[guildName]["memberUsernames"];
    const auto now = static_cast<std::int64_t>(wallClockMillis_);
    int invited = 0;
    for (std::size_t i = 0; i < members.size(); ++i) {
        const std::string member = members[i].asString();
        if (lowerCase(member) == lowerCase(session.username)) continue;
        // +1 for the invitation already in flight this iteration: an invite is
        // a claim on a seat, and the reference stops one short for it.
        if (squad->members.size() + 1 >= kMaxSquadSize) break;
        Session* target = sessionForUser(member);
        if (target == nullptr) continue;
        // A guildmate farming another biome is not someone this squad can
        // take, so they are passed over rather than sent an invitation they
        // would be refused on.
        if (!squadAcceptsBiome(*squad, squadIdOf(*target))) continue;
        if (!squads_.invite(squadIdOf(session), squadIdOf(*target), now).empty()) {
            continue;
        }
        ++invited;
        if (net::Connection* peer = listener_.find(target->connection)) {
            sendSquadInvite(*peer, session.username, true);
        }
    }
    if (invited == 0) {
        sendNotice(connection, net::NoticeSeverity::Warning,
                   "No online guildmates available to invite (or squad is full).");
        return;
    }
    sendNotice(connection, net::NoticeSeverity::Good,
               "Sent squad invites to " + std::to_string(invited) + " online guildmate(s).");
}

void GameServer::handleGuildInviteToSquad(Session& session, net::Connection& connection,
                                          ByteReader& reader) {
    const std::string raw = reader.str();
    if (!reader.ok() || !session.authenticated()) return;
    guildInviteToSquad(session, connection, raw);
}

void GameServer::guildInviteToSquad(Session& session, net::Connection& connection,
                                    const std::string& raw) {
    if (!session.authenticated()) return;

    const std::string target = trimmed(raw);
    const std::string guildName = guildNameForUser(session.username);
    if (guildName.empty() ||
        guildMemberIndex(database_.storedTable("guilds")[guildName], target) < 0) {
        sendNotice(connection, net::NoticeSeverity::Warning, target + " is not in your guild.");
        return;
    }
    Session* peerSession = sessionForUser(target);
    if (peerSession == nullptr) {
        sendNotice(connection, net::NoticeSeverity::Warning, target + " is offline.");
        return;
    }
    const std::string whileSplit =
        splitSquadRefusal(squadIdOf(session), "You are") +
        splitSquadRefusal(squadIdOf(*peerSession), target + " is");
    if (!whileSplit.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, whileSplit);
        return;
    }
    Squad* squad = squadOrCreate(session, connection);
    if (squad == nullptr) {
        sendNotice(connection, net::NoticeSeverity::Warning, "Failed to create a squad.");
        return;
    }
    const std::string wrongBiome =
        squadBiomeRefusal(*squad, squadIdOf(*peerSession), target + " is");
    if (!wrongBiome.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, wrongBiome);
        return;
    }
    const std::string error =
        squads_.invite(squadIdOf(session), squadIdOf(*peerSession),
                       static_cast<std::int64_t>(wallClockMillis_));
    if (!error.empty()) {
        sendNotice(connection, net::NoticeSeverity::Warning, error);
        return;
    }
    sendNotice(connection, net::NoticeSeverity::Good, "Squad invite sent to " + target + ".");
    if (net::Connection* peer = listener_.find(peerSession->connection)) {
        sendSquadInvite(*peer, session.username, true);
    }
}

void GameServer::serviceNearbyRevives(double nowMillis) {
    constexpr double range = 120.0;
    for (auto& entry : sessions_) {
        Session& corpse = entry.second;
        const Entity victim = corpse.entity;
        if (auto* pads = world_.tryGet<TeleporterState>(victim)) {
            if (pads->markHoldUntilMillis > 0 && nowMillis >= pads->markHoldUntilMillis) {
                pads->markHoldUntilMillis = 0;
                if (isStanding(world_, victim)) {
                    if (auto* connection = listener_.find(corpse.connection)) {
                        sendNotice(*connection, net::NoticeSeverity::Info,
                            "H Mark滞留已解除，可用 /mark-return 返回原处或正常传送。");
                    }
                }
            }
        }
        const auto* at = world_.tryGet<Transform>(victim);
        const auto* hp = world_.tryGet<Health>(victim);
        if (!corpse.playing() || !world_.has<Dead>(victim) || !at || !hp) {
            corpse.rescueHelper = NULL_ENTITY;
            continue;
        }
        const auto eligible = [&](Entity helper) {
            if (helper == victim || helper == NULL_ENTITY || world_.has<Dead>(helper)) return false;
            const auto* pos = world_.tryGet<Transform>(helper);
            const auto* health = world_.tryGet<Health>(helper);
            return pos && health && health->alive() && pos->realm == at->realm &&
                distanceSq(pos->position, at->position) <= range * range &&
                !terrain_->segmentBlocked(pos->position, at->position, at->realm);
        };
        if (!eligible(corpse.rescueHelper)) {
            corpse.rescueHelper = NULL_ENTITY;
            double nearest = range * range + 1;
            for (const auto& candidate : sessions_) {
                if (!candidate.second.playing() || !eligible(candidate.second.entity)) continue;
                const double d = distanceSq(world_.get<Transform>(candidate.second.entity).position, at->position);
                if (d < nearest) { nearest = d; corpse.rescueHelper = candidate.second.entity; }
            }
            corpse.rescueStartedMillis = nowMillis;
        }
        if (corpse.rescueHelper == NULL_ENTITY || nowMillis - corpse.rescueStartedMillis < 3000) continue;
        const Entity helper = corpse.rescueHelper;
        auto& health = world_.get<Health>(victim);
        health.current = health.max;
        health.invulnerableUntilMillis = nowMillis + kRespawnInvulnerabilitySeconds * 1000.0;
        world_.remove<Dead>(victim);
        if (world_.has<Unrevivable>(victim)) world_.remove<Unrevivable>(victim);
        onPlayerRevived(victim, helper);
    }
}

void GameServer::handleRespawn(Session& session) {
    if (!session.authenticated()) return;
    if (dungeons_ && dungeons_->inWave(world_, session.entity) && !isStanding(world_, session.entity)) {
        if (auto* connection = listener_.find(session.connection))
            sendNotice(*connection, net::NoticeSeverity::Info, "队友靠近你 3 秒可救援，清完当前波也可复活。");
        return;
    }
    // A lent console is lent for one life. Dropping it here rather than on
    // death means the grantee keeps it while they are looking at the death
    // card, which is where the reference leaves it too.
    revokeTempAdmin(session.connection);
    // Continue stands near the corpse (ashflor-style in-place respawn), not
    // back at the title-screen door. Capture the death site before the body
    // is removed.
    const bool respawnAtDoor = session.respawnAtDoor;
    bool corpseKnown = session.hasDeathSite;
    Realm corpseRealm = session.deathRealm;
    Vec2 corpsePos = session.deathPosition;
    if (session.playing() && world_.isAlive(session.entity)) {
        Health* health = world_.tryGet<Health>(session.entity);
        if (health && health->alive() && !world_.has<Dead>(session.entity)) {
            // A delayed death card or a lost revive notification must be
            // acknowledged. Repeated Continue must not heal or replace a body.
            onPlayerRevived(session.entity, NULL_ENTITY);
            return;
        }
        if (const Transform* corpse = world_.tryGet<Transform>(session.entity)) {
            corpseKnown = true;
            corpseRealm = corpse->realm;
            corpsePos = corpse->position;
        }
        despawnPlayer(session, false);
    }
    if (corpseRealm == Realm::Arena || respawnAtDoor) corpseKnown = false;
    const Entity reborn = corpseKnown ? spawnPlayerNear(session, corpseRealm, corpsePos)
                                      : spawnPlayer(session);
    // The snapshot stream restates the body but never the realm. If somehow
    // there was no corpse (session already cleared) and spawnPlayer picked a
    // different map than the client was drawing, restate the realm the same
    // way a pad does.
    if (reborn != NULL_ENTITY) {
        // An existing empty pool bypasses the first-entry mana allowance.
        world_.ensure<ManaPool>(reborn).current = 0.0;
        const Transform* transform = world_.tryGet<Transform>(reborn);
        if (transform != nullptr) {
            sendRealmChange(session, transform->position);
        }
    }
    // A fresh arena run has a fresh kit; the same restatement the join makes.
    // Revived is what drops the client's death card -- without it Continue
    // would leave the player standing with the card still up (same as a
    // yggdrasil raise that forgot to announce).
    if (net::Connection* connection = listener_.find(session.connection)) {
        sendProfile(session, *connection);
        if (reborn != NULL_ENTITY) {
            ByteWriter w;
            w.u8(static_cast<std::uint8_t>(net::ServerMessage::Revived));
            w.str("");   // self-continue; client says "You were revived."
            connection->send(w);
        }
    }
}

void GameServer::handlePing(net::Connection& connection, ByteReader& reader) {
    const std::uint64_t clientTime = reader.u64();
    if (!reader.ok()) return;
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Pong));
    w.u64(clientTime);
    w.u64(static_cast<std::uint64_t>(monotonicMillis()));
    connection.send(w);
}

// ---------------------------------------------------------------------------
// Player lifecycle
// ---------------------------------------------------------------------------

void GameServer::collectSpawnBlockers(Realm realm, std::vector<MobDisc>& out) const {
    out.clear();
    Query<MobTag, Transform, Body> mobs{const_cast<World&>(world_)};
    mobs.each([&](Entity, MobTag&, Transform& transform, Body& body) {
        if (transform.realm != realm) return;
        out.push_back({transform.position, body.radius});
    });
}

bool GameServer::canMarkPlayer(Entity target, Entity marker, Rarity rarity) {
    if (target == marker || !isStanding(world_, marker) || !world_.has<Dead>(target) ||
        world_.has<WavePlayer>(target) || world_.has<WavePlayer>(marker)) return false;
    Session* victim = sessionForEntity(target);
    Session* caster = sessionForEntity(marker);
    if (!victim || !caster || victim->entity != target || caster->entity != marker ||
        victim->splitOther != NULL_ENTITY || caster->splitOther != NULL_ENTITY ||
        victim->userId == caster->userId) return false;
    const auto* at = world_.tryGet<Transform>(target);
    const auto* from = world_.tryGet<Transform>(marker);
    const auto* progress = world_.tryGet<PlayerProgress>(target);
    const auto* ownerProgress = world_.tryGet<PlayerProgress>(marker);
    if (!at || !from || at->realm != from->realm || !progress || !ownerProgress ||
        progress->level < 20 || ownerProgress->level < 20) return false;
    const auto* map = worldMaps_.forRealm(at->realm);
    // Ordinary biomes only. Do not extract players from arena/dungeon runs or
    // move a victim around Hel repeatedly. Prestige stays in prestige.
    if (!map || (map->id() != map->biome() && !map->prestige()) ||
        map->biome() == "hel") return false;
    if (const auto* loadout = world_.tryGet<Loadout>(target)) {
        for (const auto& slot : loadout->slots) {
            if (slot.configIndex != kNoPetal &&
                petalTier(slot.rarity) > petalTier(rarity)) return false;
        }
    }
    const auto found = markProtectedUntil_.find(victim->userId);
    return found == markProtectedUntil_.end() || clockMillis_ >= found->second;
}

bool GameServer::sendMarkedPlayerToHel(Entity target, Entity marker, Rarity rarity) {
    if (!canMarkPlayer(target, marker, rarity)) return false;
    const Transform origin = world_.get<Transform>(target);
    bool found = false;
    Realm realm = worldMaps_.realmOfId("hel", found);
    if (!found) return false;
    if (worldMaps_.isPrestige(origin.realm)) {
        realm = worldMaps_.prestigeRealm(realm, found);
        if (!found) return false;
    }
    const auto* map = worldMaps_.forRealm(realm);
    if (!map) return false;
    std::vector<MobDisc> blockers;
    collectSpawnBlockers(realm, blockers);
    Vec2 spawn = map->defaultSpawn(rng_, *terrain_, &blockers);
    const auto* body = world_.tryGet<Body>(target);
    spawn = playerRespawnPosition(*terrain_, realm, spawn, body ? body->radius : 25.0);
    // Protection is per account, not per corpse/petal/caster. Expired rows are
    // discarded on application so the transient table stays bounded.
    for (auto it = markProtectedUntil_.begin(); it != markProtectedUntil_.end();) {
        if (clockMillis_ >= it->second) it = markProtectedUntil_.erase(it);
        else ++it;
    }
    Session* victim = sessionForEntity(target);
    markProtectedUntil_[victim->userId] = clockMillis_ + kDarkMarkProtectionMillis;
    auto& hp = world_.get<Health>(target);
    hp.current = hp.max;
    hp.invulnerableUntilMillis = clockMillis_ + kRespawnInvulnerabilitySeconds * 1000.0;
    world_.remove<Dead>(target);
    if (world_.has<Unrevivable>(target)) world_.remove<Unrevivable>(target);
    onPlayerRevived(target, NULL_ENTITY);
    moveEntityToRealm(target, realm, spawn);
    auto& pads = world_.ensure<TeleporterState>(target);
    const bool hyper = ladderRarity(rarity) == Rarity::Apex;
    pads.markHoldUntilMillis = hyper ? clockMillis_ + kHyperMarkHoldMillis : 0;
    pads.markOrigin = {origin.position, origin.realm};
    pads.markReturnAvailable = true;
    victim->hasDeathSite = false;
    if (auto* connection = listener_.find(victim->connection)) {
        sendNotice(*connection, net::NoticeSeverity::Info, hyper
            ? "H Mark：已传送至地狱，30秒后可用 /mark-return 返回原处。死亡或返回菜单解除滞留；5分钟内免疫其他Mark。"
            : "Mark：已传送至地狱，可立即用 /mark-return 返回原处；5分钟内免疫其他Mark。");
    }
    return true;
}

void GameServer::onPlayerRevived(Entity revived, Entity reviver) {
    if (auto* pads = world_.tryGet<TeleporterState>(revived)) pads->markHoldUntilMillis = 0;
    Session* session = sessionForEntity(revived);
    if (session == nullptr) return;

    // The corpse's death has already been announced and the reaper has stopped
    // looking at it. Clearing the flag is what lets this body die a second
    // time: without it the next death is silent and the player is left standing
    // as a corpse nobody told them about.
    session->deathReported = false;
    session->respawnAtDoor = false;
    session->rescueHelper = NULL_ENTITY;
    session->rescueStartedMillis = 0;
    net::Connection* connection = listener_.find(session->connection);
    if (connection == nullptr) return;

    // Whoever carried the yggdrasil, by the name on their nameplate -- a bot's
    // reads the same way a player's does. A reviver whose own body went away
    // between the raise and this call leaves the client its own fallback.
    std::string reviverName;
    if (reviver != NULL_ENTITY && world_.isAlive(reviver)) {
        if (const PlayerAccount* account = world_.tryGet<PlayerAccount>(reviver)) {
            reviverName = account->username;
        }
    }

    // The counterpart of the `Died` the reaper sent: without it the client
    // keeps the death card up and stops sending input, so a revived player
    // stands in the world unable to move.
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Revived));
    w.str(reviverName);
    connection->send(w);
    if (!reviverName.empty()) {
        broadcastChat(net::ChatChannel::System, "",
            escapedMarkup(world_.get<PlayerAccount>(revived).username) + " 被 " + escapedMarkup(reviverName) + " 复活了。");
    }
}

Entity GameServer::spawnPlayer(Session& session) {
    // Never on top of a body the session still holds. Both callers arrive
    // with none -- handleJoin refuses a session that is playing, and a respawn
    // takes the corpse off first -- so one still attached here is a body some
    // change of stage forgot, which is how a repeated Hello used to strand a
    // flower per join. Overwriting session.entity would leave it standing for
    // good, so it is taken out first, with any control and split it is part
    // of. Removed rather than refused: a refused join leaves the stray body
    // exactly where it is and the socket unable to play. Removed with the
    // ordinary save, which is persistPlayer's to make or refuse, and it makes
    // it only for a Playing session. In the state this exists to catch -- a
    // stage that stopped saying Playing while the body stood -- that is a
    // refusal, and the right one: the account on the session may have changed
    // while the stage said there was no body, and a save then would write one
    // account's flower into another's record. The cost is whatever the stray
    // body earned after the saves stopped seeing it.
    if (session.entity != NULL_ENTITY || session.split()) {
        std::printf("[SESSION] %s (connection %u) still held a body at spawn; removing it\n",
                    session.username.c_str(), static_cast<unsigned>(session.connection));
        despawnPlayer(session, true);
        // A split with no active half to end it through, should one ever exist.
        endSplit(session, clockMillis_, false);
    }

    // Where a player appears is a property of the MAP, not of the player.
    // Level deliberately does not enter into it: picking a zone by tier reads
    // as if a high-level flower should start in high-tier ground, and what it
    // actually does is drop everyone into the mythic band in the middle of the
    // world, which nothing can walk out of.
    // The candidate has to be clear of the mobs standing on it, not only of
    // the walls: the reference refuses a spawn point that overlaps a body or
    // that has more than a handful of mobs within 200 units, which is what
    // stops a fresh flower materialising inside a swarm.
    const Realm realm = spawnRealmFor(session);
    Vec2 spawn;
    if (realm == Realm::Arena) {
        spawn = kArenaSpawn;
    } else if (realm == Realm::Maze) {
        // Inside the entrance room, jittered so a party does not stack on one
        // point (src/server/playerManager.ts:168-179), and on floor whatever
        // the jitter did.
        spawn = terrain_->findOpenSpawn(rng_, activeMaze().spawn(), kMazeCellSize * 0.3, realm);
    } else {
        std::vector<MobDisc> blockers;
        collectSpawnBlockers(realm, blockers);
        const SpawnChoice* choice = chosenDoor(session);
        const MapData* map = worldMaps_.forRealm(realm);
        // The chosen rectangle first; its own map's default when the rectangle
        // is walled over or crowded; the overworld's default when the player
        // chose nothing at all.
        if (choice == nullptr || map == nullptr ||
            !map->spawnAt(worldMaps_.maps()[static_cast<std::size_t>(worldMapSlot(realm))]
                              .elements()[static_cast<std::size_t>(choice->element)]
                              .spawnId,
                          rng_, *terrain_, spawn, &blockers)) {
            const MapData* fallback = map != nullptr ? map : worldMaps_.forRealm(Realm::Overworld);
            spawn = fallback != nullptr ? fallback->defaultSpawn(rng_, *terrain_, &blockers)
                                        : terrain_->spawnPoint(realm);
        }
    }

    return finishPlayerSpawn(session, realm, spawn);
}

Entity GameServer::spawnPlayerNear(Session& session, Realm realm, Vec2 around) {
    if (session.entity != NULL_ENTITY || session.split()) {
        std::printf("[SESSION] %s (connection %u) still held a body at spawn; removing it\n",
                    session.username.c_str(), static_cast<unsigned>(session.connection));
        despawnPlayer(session, true);
        endSplit(session, clockMillis_, false);
    }

    const Entity entity = finishPlayerSpawn(session, realm, around);
    // The new body's level determines its hull; the base radius can leave a
    // high-level flower embedded and eject it through a rail on the next tick.
    auto& at = world_.get<Transform>(entity);
    at.position = playerRespawnPosition(*terrain_, at.realm, at.position,
                                        world_.get<Body>(entity).radius);
    return entity;
}

Entity GameServer::finishPlayerSpawn(Session& session, Realm realm, Vec2 spawn) {
    const Realm requestedRealm = realm;
    routePyramidEntrance(realm, spawn);
    // The realm is settled on the session BEFORE the account is applied, and
    // the arena's scratch record with it: applyAccountToEntity reads both.
    session.realm = realm;
    session.arena.reset();
    if (realm == Realm::Arena) session.arena = startArenaRun(database_.progress(session.userId));

    const Entity entity = createPlayerBody(session, realm, spawn);
    if (realm != requestedRealm) {
        world_.get<Transform>(entity).position =
            playerRespawnPosition(*terrain_, realm, spawn, world_.get<Body>(entity).radius);
    }

    // A FRESH body only: full health and the respawn window. createPlayerBody
    // must never do either -- a splitter's second body is built by it too, and
    // it inherits what the flower it was cut from was carrying.
    Health& health = world_.get<Health>(entity);
    health.current = health.max;
    health.invulnerableUntilMillis = clockMillis_ + kRespawnInvulnerabilitySeconds * 1000.0;

    // Respawning in the same pyramid continues the current visit.
    if (session.pyramidMapRealm != realm) session.pyramidMapRevealed = false;
    session.entity = entity;
    session.stage = SessionStage::Playing;
    // A fresh body has a death of its own still to announce.
    session.deathReported = false;
    session.hasDeathSite = false;
    session.respawnAtDoor = false;
    // And a fresh life owes nothing to the last one's losses: a splitter on
    // the bar cuts this body in two on the next tick.
    session.splitReadyAtMillis = 0;
    // A player picks their door on the title screen, and a squad they joined
    // before going back to it is a squad they may now be standing three
    // biomes away from. This is the hole the join-time checks cannot close,
    // because nothing about the join goes through them.
    enforceSquadBiome(squadIdOf(session));
    // The roster carries WIRE IDS, and this player just acquired a new one.
    // Without this a squadmate's dot and party bar stay pinned to the body
    // they had before they died.
    if (Squad* squad = squads_.forMember(squadIdOf(session))) broadcastSquadUpdate(*squad);
    return entity;
}

Entity GameServer::createPlayerBody(Session& session, Realm realm, Vec2 spawn) {
    const Entity entity = world_.create();
    world_.add<PlayerTag>(entity);
    world_.add<Transform>(entity, Transform{spawn, 0.0, realm});
    world_.add<Motion>(entity);
    world_.add<Knockback>(entity);
    // In the ring players are hostile to each other; everywhere else they are
    // not. Petals and pets copy this Faction, so the whole kit follows.
    world_.add<Faction>(entity, Faction{Team::Players, realm == Realm::Arena});
    if (realm == Realm::Arena) world_.add<ArenaScore>(entity);
    world_.add<PlayerInput>(entity);
    world_.add<PlayerLocation>(entity);
    world_.add<PlayerModifiers>(entity);
    world_.add<PlayerVisuals>(entity);
    world_.add<PlayerSkillTree>(entity);
    world_.add<Loadout>(entity);
    world_.add<PetalRing>(entity);
    // A flower's body damages mobs on contact in the TypeScript server. The
    // zero interval means every separated re-contact may land; the fixed
    // 25-unit bump normally prevents it from becoming a per-tick damage beam.
    world_.add<ContactDamage>(entity, ContactDamage{kPlayerBaseDamage, 0.0});
    world_.add<HitCooldowns>(entity);
    world_.add<Afflictions>(entity);
    world_.add<ShieldState>(entity);
    // The nameplate carries the flower's name, which is what the title screen
    // asked for; the account name stays on the session for chat and for saves.
    world_.add<PlayerAccount>(entity,
                              PlayerAccount{session.userId,
                                            session.displayName.empty() ? session.username
                                                                        : session.displayName,
                                            session.connection, session.admin});

    const PlayerRecord& record = liveRecord(session);
    applyAccountToEntity(record, entity);

    world_.add<NetId>(entity, NetId{netIds_.next()});
    Replicated replicated;
    replicated.kind = net::EntityKind::Player;
    world_.add<Replicated>(entity, replicated);
    return entity;
}

void GameServer::applyAccountToEntity(const PlayerRecord& record, Entity entity) {
    // Which realm the body is in decides which of the account's tracks it
    // plays on: the maze has its own XP and its own tree
    // (src/server/playerManager.ts:198-245), and the ring has a flat health
    // pool with talents switched off (src/server/playerManager.ts:812-824).
    const Transform* transform = world_.tryGet<Transform>(entity);
    const Realm realm = transform != nullptr ? transform->realm : Realm::Overworld;
    const bool maze = realm == Realm::Maze;
    const SkillSet& skills = maze ? record.mazeSkills : record.skills;

    // XP is seeded from the account ONCE, when the body is built. After that
    // the body is the authority: kills pay the live component, and the record
    // only catches up when persistPlayer runs -- every 30 seconds, on death
    // and on leave. Re-seeding here, which every loadout edit and swap does,
    // rewound the flower to its last save: a level gained since then was
    // shown for a few frames and taken back, and the next save wrote the
    // rewound figure over the XP for good.
    const bool fresh = !world_.has<PlayerProgress>(entity);
    PlayerProgress& state = world_.ensure<PlayerProgress>(entity);
    if (fresh) {
        state.totalXp = maze ? record.mazeTotalXp : record.totalXp;
        state.level = levelFromTotalXp(state.totalXp).level;
    }
    state.stars = record.stars;
    // Copied out: the ensure<>() calls below can move this row to another
    // archetype and leave `state` pointing at whatever took its slot.
    const int level = state.level;

    // Cosmetic skin bits are account data, and so is the worn custom skin --
    // the body is rebuilt on every respawn, so without this line a player who
    // dies comes back a plain flower. The temporary glitch bit stays on
    // PlayerVisuals and is intentionally not reset by a loadout edit.
    PlayerVisuals& cosmetics = world_.ensure<PlayerVisuals>(entity);
    cosmetics.renderFlags = record.renderFlags;
    cosmetics.equippedSkinId = record.equippedSkinId;

    // The tree is copied onto the body so the tick never reaches into storage.
    // Every path that changes it -- login, respawn, buying a tier, a reset --
    // comes back through here, which is what keeps the two in step.
    world_.ensure<PlayerSkillTree>(entity).skills = skills;

    const bool freshBody = !world_.has<Body>(entity);
    Body& body = world_.ensure<Body>(entity);
    // A loadout edit must not shrink the collision hull for the movement tick
    // before petal modifiers are folded again (notably during a bubble dash).
    if (freshBody) body.radius = playerRadiusForLevel(level);
    body.mass = 1.0;

    Health& health = world_.ensure<Health>(entity);
    const double previousFraction = health.max > 0 ? health.current / health.max : 1.0;
    health.max = realm == Realm::Arena
                     ? kArenaMaxHealth
                     : maxHealthForLevel(level) * skills.statScale(SkillId::Health);
    // Preserve the FRACTION across a max-health change, so levelling up mid
    // fight neither heals you to full nor leaves you proportionally worse off.
    //
    // Clamped DOWNWARD only. This function also runs on every loadout edit and
    // every talent purchase, including ones sent from the death screen -- the
    // client keeps its panels live there and a corpse keeps its session. A
    // rescue that read "empty means full" would refill a dead flower's health
    // bar in front of everyone watching it, and arming respawn protection here
    // would hand out three seconds of immunity for the price of swapping two
    // empty loadout slots, over and over. Both belong to a fresh body, so both
    // live in spawnPlayer.
    health.current = clamp(health.max * clamp(previousFraction, 0.0, 1.0), 0.0, health.max);

    world_.ensure<ContactDamage>(entity).amount = bodyDamageForLevel(level);
    world_.get<ContactDamage>(entity).intervalMillis = 0.0;

    Loadout& loadout = world_.ensure<Loadout>(entity);
    for (std::size_t i = 0; i < kLoadoutSlots; ++i) {
        const WornSlot worn = wornSlot(record, i, realm);
        loadout.slots[i].configIndex = worn.petalIndex;
        loadout.slots[i].rarity = worn.rarity;
        // `broken` and `reloadReadyAtMillis` are deliberately NOT touched. They
        // are the ring's own bookkeeping: the petal pass arms a full reload on
        // any slot whose contents changed and leaves an unchanged one alone, so
        // clearing them here would wipe the timer of a slot the player did not
        // touch, and would make re-equipping the same petal over one that just
        // broke a way to dodge its reload entirely.
    }
}

void GameServer::applyAccountToSession(Session& session) {
    const PlayerRecord& record = liveRecord(session);
    for (const Entity body : bodiesOf(session)) {
        if (body != NULL_ENTITY && world_.isAlive(body)) applyAccountToEntity(record, body);
    }
}

void GameServer::bankExperience() {
    // Combat pays the live body, including squad members without a gallery
    // kill. Use the merged splitter progress before a death can reap the body.
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.playing() || session.realm == Realm::Arena) continue;
        const PlayerProgress* progress = world_.tryGet<PlayerProgress>(session.entity);
        if (progress == nullptr) continue;

        const auto* current = database_.findProgress(session.userId);
        if (current && (session.realm == Realm::Maze ? current->mazeTotalXp : current->totalXp) == progress->totalXp)
            continue;
        PlayerRecord& record = database_.progress(session.userId);
        double& trackXp = session.realm == Realm::Maze ? record.mazeTotalXp : record.totalXp;
        trackXp = progress->totalXp;
        database_.markDirty();
        session.profileDue = true;
    }
}

void GameServer::persistPlayer(const Session& session) {
    if (!session.playing() || !world_.isAlive(session.entity)) return;
    const PlayerProgress* progress = world_.tryGet<PlayerProgress>(session.entity);
    if (!progress) return;

    PlayerRecord& record = database_.progress(session.userId);
    // Onto the track the body was playing on: a maze run's XP is the maze's,
    // and writing it into totalXP would destroy the outside level
    // (src/server/playerManager.ts:975-978).
    if (session.realm == Realm::Maze) record.mazeTotalXp = progress->totalXp;
    else record.totalXp = progress->totalXp;
    record.stars = progress->stars;
    database_.markDirty();
}

void GameServer::despawnPlayer(Session& session, bool persist) {
    if (session.entity == NULL_ENTITY) return;
    // Control needs a flower at both ends, so the body going is the end of
    // any this session is part of: whoever was steering it is put back on
    // their own flower and told why, and a flower this one was steering is
    // handed back to its player. Through the back-references, and while the
    // body still exists to be parked and named.
    endControlOf(session, "left the world", true);
    // The other half first, and with no reload armed: the body it would have
    // been armed on is about to be destroyed as well, and the split is ending
    // because the PLAYER is leaving, not because a flower was lost.
    endSplit(session, clockMillis_, false);
    if (persist) persistPlayer(session);
    // Leaving the ring, by any door, is the end of the run.
    if (session.arena) endArenaRun(session);
    session.realm = Realm::Overworld;

    // The petals and pets belong to the body, not the account, so they go
    // with it.
    destroyBody(session.entity);

    session.entity = NULL_ENTITY;
    session.stage = SessionStage::Authenticated;
    views_[session.connection].reset();
    // Same reason as the spawn: the id this member was known by is gone, and a
    // roster still naming it points every squadmate's HUD at nothing.
    if (Squad* squad = squads_.forMember(squadIdOf(session))) broadcastSquadUpdate(*squad);
}

void GameServer::destroyBody(Entity body) {
    if (body == NULL_ENTITY || !world_.isAlive(body)) return;
    // The ring is on the Loadout; the pets are on the SLOT STATE, and the only
    // other thing that ever walks those is the ring pass recalling a downed
    // flower's summons (PetalSystem::clearRing). A body destroyed while it was
    // standing is never down, so without this its pets outlived it with a
    // dangling owner -- and an ownerless pet wanders for good.
    //
    // Destroyed, not killed, exactly as a recall does it: nothing dies here,
    // so nothing raises a death event or drops anything. A handle the world
    // already reaped is harmless: handles are generational.
    if (const Loadout* loadout = world_.tryGet<Loadout>(body)) {
        for (const Entity petal : loadout->spawned) commands_.destroy(petal);
    }
    if (const PetalSlotState* state = world_.tryGet<PetalSlotState>(body)) {
        for (const PetalSlotState::Slot& slot : state->slots) {
            for (const Entity pet : slot.pets) commands_.destroy(pet);
        }
    }
    commands_.destroy(body);
}

// ---------------------------------------------------------------------------
// Realms: the arena run and the maze track
// ---------------------------------------------------------------------------

void GameServer::moveEntityToRealm(Entity entity, Realm realm, Vec2 position) {
    if (!world_.isAlive(entity)) return;
    Transform* transform = world_.tryGet<Transform>(entity);
    if (transform == nullptr) return;
    const Realm requestedRealm = realm;
    routePyramidEntrance(realm, position);
    if (realm != requestedRealm) {
        if (const auto* body = world_.tryGet<Body>(entity)) {
            position = playerRespawnPosition(*terrain_, realm, position, body->radius);
        }
    }
    if (transform->realm != realm && isStanding(world_, entity)) {
        const auto* pads = world_.tryGet<TeleporterState>(entity);
        if (pads && clockMillis_ < pads->markHoldUntilMillis) return;
    }
    if (transform->realm == realm && distanceSq(transform->position, position) < 1.0) return;

    const Realm from = transform->realm;
    const bool sameRealm = from == realm;
    if (!sameRealm) {
        if (auto* pads = world_.tryGet<TeleporterState>(entity)) pads->markReturnAvailable = false;
    }
    transform->realm = realm;
    transform->position = position;
    // Velocity and knockback describe where the body WAS going, in a space it
    // is no longer in. A flower that arrives still carrying the impulse that
    // pushed it onto the pad arrives sliding.
    if (Motion* motion = world_.tryGet<Motion>(entity)) motion->velocity = {0, 0};
    if (Knockback* knockback = world_.tryGet<Knockback>(entity)) {
        knockback->impulse = {0, 0};
        knockback->carry = {0, 0};
    }
    // A pad the flower is standing on at the far end must not grab it back on
    // the very next tick, and its charge-up belongs to the map it was on.
    if (TeleporterState* pads = world_.tryGet<TeleporterState>(entity)) {
        pads->pad = -1;
        pads->enteredAtMillis = 0;
        pads->cooldownUntilMillis = clockMillis_ + kTeleporterCooldownMillis;
        pads->arriving = true;
    }
    // Ahead of the RealmChange below, so the client still has the frame from
    // before the jump to close the iris over.
    sendTeleported(entity);

    // A pad that leads somewhere on the SAME map is a jump, not a move
    // between worlds: the grid under the body has not changed, so there is
    // nothing to send and nothing to clear. The kit is re-placed around the
    // ring by its own system, and the client's snap-distance rule cuts the
    // interpolation on its own, exactly as it does for teleportEntity().
    if (sameRealm) return;

    // The kit comes too. A petal or a pet left behind in the old realm is not
    // merely invisible: it is in another coordinate space, so it never reaches
    // its owner's ring again, it is streamed to nobody, and it goes on
    // colliding with whatever it was left standing next to.
    //
    // One pass over the world rather than a per-owner index, because this runs
    // when somebody takes a teleporter and not per tick.
    Query<Transform> everything{world_};
    everything.each([&](Entity other, Transform& at) {
        if (other == entity) return;
        if (at.realm != from) return;
        // Except a loose petal: put down HERE it would land on the flower and
        // have to be shoved off it. The petal pass finds it in the old realm on
        // its next run and puts it down behind the flower instead
        // (PetalSystem::carryLoosePetalsAcrossRealms).
        if (world_.has<LoosePetal>(other)) return;
        Entity owner = NULL_ENTITY;
        if (const PetalInstance* petal = world_.tryGet<PetalInstance>(other)) owner = petal->owner;
        else if (const Pet* pet = world_.tryGet<Pet>(other)) owner = pet->owner;
        else if (const Projectile* shot = world_.tryGet<Projectile>(other)) owner = shot->creditTo;
        if (owner != entity) return;
        at.realm = realm;
        // Petals are re-placed around the ring by their own system on the next
        // tick; putting them on the flower now keeps them out of whatever the
        // destination has at their old coordinates in the meantime.
        at.position = position;
    });

    // The client has no map file: it has to be SENT the destination's grid, or
    // it draws the map it arrived from underneath a body standing somewhere
    // else entirely.
    Session* session = sessionForEntity(entity);
    if (session == nullptr) {
        // A bot: nothing to tell, but a bot in a squad has still just walked
        // out of the biome the rest of it is working.
        if (botForEntity(entity) != nullptr) enforceSquadBiome(SquadMemberId::ofBot(entity));
        return;
    }
    // The PARKED half of a split went through on its own. sessionForEntity
    // matches either half, but the client is looking through the steered one,
    // which has not moved: restating its view would load the far map under a
    // flower still standing on this one. The parked half simply waits over
    // there, and switching to it (switchSplitHalf) is what sends the client.
    if (entity != session->entity) return;
    session->realm = realm;
    sendRealmChange(*session, position);
    enforceSquadBiome(squadIdOf(*session));
}

void GameServer::sendRealmChange(Session& session, Vec2 position) {
    views_[session.connection].reset();
    net::Connection* connection = listener_.find(session.connection);
    if (connection == nullptr) return;
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::RealmChange));
    w.position(position);
    writeMapGrid(w, *terrain_, session.realm);
    connection->send(w);
    if (session.pyramidMapRealm != session.realm) session.pyramidMapRevealed = false;
    session.pyramidMapRealm = session.realm;
    sendPyramidMapState(session);
}

void GameServer::sendTeleported(Entity entity) {
    Session* session = sessionForEntity(entity);
    if (session == nullptr || entity != session->entity) return;
    net::Connection* connection = listener_.find(session->connection);
    if (connection == nullptr) return;
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::Teleported));
    connection->send(w);
}

std::string GameServer::biomeOfRealm(Realm realm) const {
    // The two generated realms answer with the picker ids they are asked for
    // by name, so a ring party and a maze party are each one biome rather
    // than sharing the nameless bucket every unstaged realm would fall into.
    if (realm == Realm::Arena) return kArenaSpawnChoice;
    if (realm == Realm::Maze) return kMazeSpawnChoice;
    const MapData* map = worldMaps_.forRealm(realm);
    if (map == nullptr) return std::string();
    // A twin is a biome of its own: nobody in the garden can see a flower in
    // the prestige garden, so the two can no more share a squad than the
    // garden and the desert can.
    return map->prestige() ? kPrestigeMapPrefix + map->biome() : map->biome();
}

std::string GameServer::biomeLabel(const std::string& biome) const {
    if (biome.empty()) return biome;
    const std::string plain = stripPrestigePrefix(biome);
    if (plain != biome) return std::string(kPrestigeLabel) + " " + biomeLabel(plain);
    for (const SpawnChoice& choice : worldMaps_.spawnChoices()) {
        if (biomeOfRealm(choice.realm) == biome) return choice.label;
    }
    if (biome == kArenaSpawnChoice) return "PVP Arena";
    if (biome == kMazeSpawnChoice) return "Maze";
    return biome;
}

std::string GameServer::biomeOfEntity(Entity entity) const {
    if (entity == NULL_ENTITY || !world_.isAlive(entity)) return {};
    const Transform* transform = world_.tryGet<Transform>(entity);
    if(!transform)return {};
    Realm exterior;Vec2 entrance;
    if (dungeons_ && (dungeons_->waveExterior(world_, entity, exterior) ||
                     dungeons_->exteriorForRealm(transform->realm, exterior, entrance))) return biomeOfRealm(exterior);
    return biomeOfRealm(transform->realm);
}

const SpawnChoice* GameServer::chosenDoor(const Session& session) const {
    const SpawnChoice* door = worldMaps_.door(session.spawnChoice);
    if (door == nullptr) return nullptr;
    // Checked on every body, not only at the join: a lent console is lent
    // for one life, and a non-pickable door named on it is not kept past it.
    if (!door->pickable && !session.admin) return nullptr;
    return door;
}

Realm GameServer::spawnRealmFor(const Session& session) const {
    if (session.spawnChoice == kArenaSpawnChoice) return Realm::Arena;
    if (session.spawnChoice == kMazeSpawnChoice) return Realm::Maze;
    // A spawn point carries the map it is on, so choosing one is how a player
    // joins straight into a map other than the overworld.
    const SpawnChoice* choice = chosenDoor(session);
    const Realm plain = choice != nullptr ? choice->realm : Realm::Overworld;
    if (!session.prestige) return plain;
    // The same door on the map's prestige twin: the twin is the same file, so
    // the door's element index means the same rectangle on it.
    bool found = false;
    const Realm twin = worldMaps_.prestigeRealm(plain, found);
    return found ? twin : plain;
}

PlayerRecord& GameServer::liveRecord(Session& session) {
    if (session.arena) return *session.arena;
    return database_.progress(session.userId);
}

std::unique_ptr<PlayerRecord> GameServer::startArenaRun(const PlayerRecord& account) const {
    // The reference's enterPvpArena: five common basics and five empty slots,
    // nothing in the bag, and a fresh score. The level is the account's -- XP
    // keeps accruing in the ring -- and so are the cosmetics. No talents: the
    // ring switches every multiplier off, so the scratch tree stays empty.
    auto run = std::make_unique<PlayerRecord>();
    run->totalXp = account.totalXp;
    run->stars = account.stars;
    run->renderFlags = account.renderFlags;
    run->equippedSkinId = account.equippedSkinId;
    run->loadout.assign(kLoadoutSlots, std::nullopt);
    const std::uint16_t basic = content().petalIndex("basic");
    if (basic != kInvalidIndex) {
        for (std::size_t i = 0; i < 5 && i < kLoadoutActiveSlots; ++i) {
            StoredItem item;
            item.type = "petal";
            item.petalType = content().petal(basic).id;
            item.rarity = Rarity::Common;
            run->loadout[i] = item;
        }
    }
    return run;
}

void GameServer::endArenaRun(Session& session) {
    if (!session.arena) return;
    PlayerRecord& account = database_.progress(session.userId);
    // A quarter of every stack looted in the ring, rounded down, reaches the
    // account (exitPvpArena); the ring's own petals and whatever was equipped
    // out of its bag do not.
    const Json& bag = session.arena->inventory;
    for (const std::string& rarityName : bag.keys()) {
        const Rarity rarity = parseRarity(rarityName);
        const Json& byType = bag[rarityName];
        for (const std::string& key : byType.keys()) {
            const int kept = static_cast<int>(
                std::floor(PlayerRecord::stackCount(byType[key]) * kArenaInventoryKeepRatio));
            if (kept <= 0) continue;
            account.addItem(rarity, key, kept);
        }
    }
    session.arena.reset();
    database_.markDirty();
}

void GameServer::settleArenaDeath(Session& victim, Entity killer) {
    if (!victim.arena) return;
    Session* winner = killer != NULL_ENTITY ? sessionForEntity(killer) : nullptr;
    // Only another flower IN THE RING inherits anything: a mob kill, a bot, or
    // a killer who has since left simply empties the run (playerState.ts:1352).
    if (winner != nullptr && winner != &victim && winner->arena && winner->playing()) {
        if (ArenaScore* mine = world_.tryGet<ArenaScore>(victim.entity)) {
            if (ArenaScore* theirs = world_.tryGet<ArenaScore>(winner->entity)) {
                theirs->score += mine->score;
            }
        }
        const Json& bag = victim.arena->inventory;
        for (const std::string& rarityName : bag.keys()) {
            const Rarity rarity = parseRarity(rarityName);
            const Json& byType = bag[rarityName];
            for (const std::string& key : byType.keys()) {
                const int count = PlayerRecord::stackCount(byType[key]);
                if (count > 0) winner->arena->addItem(rarity, key, count);
            }
        }
        if (net::Connection* connection = listener_.find(winner->connection)) {
            sendProfile(*winner, *connection);
        }
    }
    if (ArenaScore* mine = world_.tryGet<ArenaScore>(victim.entity)) mine->score = 0;
    victim.arena->inventory = Json::object();
    if (net::Connection* connection = listener_.find(victim.connection)) {
        sendProfile(victim, *connection);
    }
}

void GameServer::sendMazeInfo(net::Connection& connection) {
    ByteWriter w;
    w.u8(static_cast<std::uint8_t>(net::ServerMessage::MazeInfo));
    w.i64(activeMaze().day());
    connection.send(w);
}

void GameServer::broadcastMazeInfo() {
    for (auto& entry : sessions_) {
        if (!entry.second.authenticated()) continue;
        if (net::Connection* connection = listener_.find(entry.second.connection)) {
            sendMazeInfo(*connection);
        }
    }
}


void GameServer::sendPyramidMapState(Session& session) {
    if (auto* connection = listener_.find(session.connection)) {
        ByteWriter w; w.u8(static_cast<std::uint8_t>(net::ServerMessage::PyramidMapState));
        w.u8(static_cast<std::uint8_t>(session.realm)); w.boolean(session.pyramidMapRevealed);
        connection->send(w);
    }
}

Realm GameServer::activePyramidRealm(Realm realm) const {
    const auto* map = worldMaps_.forRealm(realm);
    if (!map || map->biome() != "pyramid" || pyramidCycle_ < 0 || pyramidLayouts_.empty()) return realm;

    const Realm active = pyramidLayouts_[static_cast<std::size_t>(pyramidCycle_) % pyramidLayouts_.size()];
    if (!map->prestige()) return active;
    bool found = false;
    const auto twin = worldMaps_.prestigeRealm(active, found);
    return found ? twin : realm;
}

void GameServer::updatePyramidSpawnRealms() {
    if (pyramidLayouts_.empty()) return;
    const Realm active = activePyramidRealm(pyramidLayouts_.front());
    bool found = false;
    const Realm twin = worldMaps_.prestigeRealm(active, found);
    spawning_->setActivePyramidRealms(active, twin);
}

void GameServer::routePyramidEntrance(Realm& realm, Vec2& position) {
    const Realm active = activePyramidRealm(realm);
    if (active == realm) return;
    realm = active;
    const auto entrances = worldMaps_.forRealm(realm)->playerSpawns();
    if (entrances.empty()) {
        position = terrain_->spawnPoint(realm);
    } else {
        const auto& bounds = entrances.front()->bounds;
        position = {bounds.x + bounds.w * 0.5, bounds.y + bounds.h * 0.5};
    }
}

void GameServer::servicePyramidRotation(std::int64_t cycle) {
    if (cycle == pyramidCycle_) return;
    pyramidCycle_ = cycle;
    updatePyramidSpawnRealms();

    // Gather first: a realm change moves the player's pets and can update
    // the session and the archetypes that hold its teleporter state.
    std::vector<Entity> leaving;
    Query<PlayerTag, Transform>{world_}.each([&](Entity e, PlayerTag&, Transform& at) {
        if (activePyramidRealm(at.realm) != at.realm) leaving.push_back(e);
    });
    for (const Entity player : leaving) {
        const auto old = world_.get<Transform>(player).realm;
        Realm realm = old;
        Vec2 at;
        routePyramidEntrance(realm, at);
        if (const auto* body = world_.tryGet<Body>(player)) {
            at = playerRespawnPosition(*terrain_, realm, at, body->radius);
        }
        if (auto* pads = world_.tryGet<TeleporterState>(player)) pads->markHoldUntilMillis = 0.0;
        moveEntityToRealm(player, realm, at);
        if (const auto* session = sessionForEntity(player); session && session->entity == player) {
            if (auto* connection = listener_.find(session->connection)) {
                sendSystem(*connection, "金字塔地形已切换，已回到出生点。");
            }
        }
    }
    // Remove retired mobs before the population pass counts the new layout.
    // Players' pets were moved with them. Scrolls are scoped to each visit.
    std::vector<Entity> retiredMobs;
    Query<MobTag, Transform>{world_}.each([&](Entity e, MobTag&, Transform& at) {
        if (!world_.has<Pet>(e) && activePyramidRealm(at.realm) != at.realm) retiredMobs.push_back(e);
    });
    for (const Entity mob : retiredMobs) world_.destroy(mob);
    for (auto& [key, stock] : pyramidMaps_) {
        const auto realm = static_cast<Realm>(key);
        if (activePyramidRealm(realm) == realm) continue;
        for (const Entity scroll : stock.scrolls) commands_.destroy(scroll);
        stock = {};
    }
}

void GameServer::servicePyramidMaps(double now) {
    // Separate RNG and bounded stock: these are navigation aids, never 30x loot rolls.
    for (const auto& map : worldMaps_.maps()) {
        if (map.biome() != "pyramid" || map.prestige() || activePyramidRealm(map.realm()) != map.realm()) continue;
        const Realm realm = map.realm();
        auto& stock = pyramidMaps_[static_cast<unsigned>(realm)];
        auto& scrolls = stock.scrolls;
        scrolls.erase(std::remove_if(scrolls.begin(), scrolls.end(),
            [&](Entity e) { return !world_.isAlive(e); }), scrolls.end());
        if (now >= stock.refillAt && scrolls.size() < 40) {
            stock.refillAt = now + 30000.0;
            const auto extent = terrain_->realmExtent(realm);
            const auto doors = map.playerSpawns();
            for (int tries = 0; scrolls.size() < 40 && tries < 3000; ++tries) {
                Vec2 at{pyramidMapRng_.unit() * extent.x, pyramidMapRng_.unit() * extent.y};
                if (scrolls.size() < 8 && !doors.empty()) {
                    const auto& door = doors.front()->bounds;
                    at = Vec2{door.x + door.w * 0.5, door.y + door.h * 0.5} +
                         Vec2::fromAngle(pyramidMapRng_.angle(), 800.0 + pyramidMapRng_.unit() * 1200.0);
                }
                if (at.x < 32 || at.y < 32 || at.x > extent.x - 32 || at.y > extent.y - 32 ||
                    terrain_->blocked(at, realm) || terrain_->inWater(at, realm) ||
                    distance(terrain_->resolveCircle(at, 30.0, realm), at) > 0.01) continue;
                bool crowded = false;
                for (Entity e : scrolls) if (distance(world_.get<Transform>(e).position, at) < 140) { crowded = true; break; }
                if (crowded) continue;
                const Entity e = world_.create();
                world_.add<Transform>(e, Transform{at, 0, realm});
                world_.add<Body>(e, Body{26.0, 0.0});
                world_.add<Replicated>(e, Replicated{net::EntityKind::Effect, 0,
                    static_cast<std::uint16_t>(GroundEffectKind::PyramidMap), Rarity::Common});
                world_.add<NetId>(e, NetId{netIds_.next()});
                scrolls.push_back(e);
            }
        }
    }
    for (auto& [id, session] : sessions_) {
        if (!session.playing() || !world_.isAlive(session.entity)) continue;
        const auto* at = world_.tryGet<Transform>(session.entity);
        if (!at) continue;
        if (session.pyramidMapRealm != at->realm) {
            session.pyramidMapRealm = at->realm; session.pyramidMapRevealed = false;
        }
        if (session.pyramidMapRevealed || world_.has<Dead>(session.entity)) continue;
        auto found = pyramidMaps_.find(static_cast<unsigned>(at->realm));
        if (found == pyramidMaps_.end()) continue;
        const double reach = 70.0 + world_.get<Body>(session.entity).radius;
        for (Entity e : found->second.scrolls) {
            if (!world_.isAlive(e) || distance(at->position, world_.get<Transform>(e).position) > reach) continue;
            session.pyramidMapRevealed = true; world_.destroy(e);
            found->second.refillAt = now + 30000.0;
            sendPyramidMapState(session);
            if (auto* peer = listener_.find(id)) sendNotice(*peer, net::NoticeSeverity::Info,
                "已找到金字塔地图！本次探索可查看小地图，离开后失效。");
            break;
        }
    }
}


bool GameServer::expireTimedHammer(PlayerRecord& record) {
    const double now=static_cast<double>(database_.nowMillis());
    const bool temporary=expireTemporaryHyperHammer(record,now);
    const bool expired=expireHammerLeases(record,now)>0||temporary;
    database_.markDirty();return expired;
}
void GameServer::serviceTimedHammers(double now) {
    if (now < nextTimedHammerSweep_) return;
    nextTimedHammerSweep_ = now + 1000;
    for (auto& [id, session] : sessions_) {
        if (!session.authenticated()) continue;
        // Reading the sweep eligibility must not freeze an account's entire
        // inventory for a pending save. Only hammer owners need a writable row.
        const auto* record = database_.findProgress(session.userId);
        if (!record || (record->extra["titanHammerLeases"].size() == 0 &&
                       !(record->extra["titanHammerExpiresAt"].asDouble(0) > 0) &&
                       !record->extra["temporaryHyperHammer"]["active"].asBool(false))) continue;
        if (!expireTimedHammer(database_.progress(session.userId))) continue;
        if(session.playing())applyAccountToSession(session);
        if(auto* connection=listener_.find(id)) {
            sendNotice(*connection,net::NoticeSeverity::Info,"限时雷神之锤已到期，可前往泰坦重新兑换。");
            sendProfile(session,*connection);
        }
    }
}

} // namespace flix
