#pragma once
// Talent tree aligned with florrclone (mirrored client talent table).
//
// Talent points are DERIVED: one per level, minus what tiers cost. The save
// stores only per-branch levels (0 = unbought in florrclone JSON; we store
// tier index 0..n-1 for the highest owned tier, or -1 when none).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flix {

enum class SkillId : std::uint8_t {
    Health = 0,
    Loadout,
    Rotation,
    Antennae,
    Luck,
    Medic,
    PetalHealth,
    Poison,
    Reach,
    Reload,
    SharpEdges,
    SecondChance,
    Magnetism,
    Duplicator,
    BodyToxicity,
    ConcentratedPoison,
    Summoner,
    Count,
};

inline constexpr int kSkillCount = static_cast<int>(SkillId::Count);

/// Database / wire keys — exact florrclone `save.talents` field names.
inline constexpr std::array<const char*, kSkillCount> kSkillKeys = {
    "health", "loadout", "rotation", "antennae", "luck", "medic", "petalHealth",
    "poison", "reach", "reload", "sharpEdges", "secondChance", "magnetism",
    "duplicator", "bodyToxicity", "concentratedPoison", "summoner",
};

inline constexpr std::array<const char*, kSkillCount> kSkillLabels = {
    "Health", "Loadout", "Petal Rotation", "Antennae", "Luck", "Medic",
    "Petal Health", "Poison", "Reach", "Reload", "Sharp Edges", "Second Chance",
    "Magnetism", "Duplicator", "Body Toxicity", "Concentrated Poison", "Summoner",
};

inline constexpr std::array<const char*, kSkillCount> kSkillLabelsZh = {
    "血量", "槽位", "转速", "触角", "幸运", "治疗", "花瓣耐久", "剧毒", "攻击范围",
    "刷新", "锐利边缘", "第二次机会", "磁性", "重瓣", "身体毒性", "浓缩剧毒", "召唤师",
};

/// How many buyable tiers each branch has (florrclone costs[].length).
inline constexpr std::array<int, kSkillCount> kSkillTiers = {
    9, 5, 5, 3, 9, 9, 9, 9, 3, 9, 8, 2, 1, 4, 1, 1, 9,
};

/// Per-tier point costs from florrclone.
inline constexpr std::array<std::array<int, 9>, kSkillCount> kSkillCosts = {{
    {{2, 5, 8, 11, 14, 17, 20, 21, 24}},       // health
    {{3, 6, 9, 12, 15, 0, 0, 0, 0}},             // loadout
    {{1, 2, 3, 4, 5, 0, 0, 0, 0}},               // rotation
    {{1, 3, 5, 0, 0, 0, 0, 0, 0}},               // antennae
    {{2, 3, 4, 5, 6, 7, 8, 9, 10}},              // luck
    {{2, 5, 8, 11, 14, 17, 20, 23, 26}},         // medic
    {{2, 4, 6, 8, 10, 12, 14, 16, 18}},          // petalHealth
    {{1, 2, 3, 3, 3, 3, 3, 3, 3}},               // poison
    {{3, 6, 9, 0, 0, 0, 0, 0, 0}},               // reach
    {{2, 6, 10, 12, 16, 20, 24, 28, 32}},        // reload
    {{2, 3, 4, 5, 6, 7, 8, 9, 0}},               // sharpEdges
    {{10, 20, 0, 0, 0, 0, 0, 0, 0}},             // secondChance
    {{15, 0, 0, 0, 0, 0, 0, 0, 0}},              // magnetism
    {{10, 20, 30, 40, 0, 0, 0, 0, 0}},           // duplicator
    {{10, 0, 0, 0, 0, 0, 0, 0, 0}},              // bodyToxicity
    {{15, 0, 0, 0, 0, 0, 0, 0, 0}},              // concentratedPoison
    {{2, 5, 8, 11, 14, 17, 20, 23, 26}},         // summoner
}};

inline int skillTierCost(SkillId id, int tier) {
    const std::size_t i = static_cast<std::size_t>(id);
    if (i >= kSkillCount || tier < 0 || tier >= kSkillTiers[i]) return 0;
    return kSkillCosts[i][static_cast<std::size_t>(tier)];
}

inline int skillTierCount(SkillId id) {
    const std::size_t i = static_cast<std::size_t>(id);
    return i < kSkillCount ? kSkillTiers[i] : 0;
}

// Child → parent requirement (florrclone `requires`).
struct SkillGate {
    SkillId child;
    SkillId parent;
    int parentLevel;  ///< 1-based owned level required on parent
};

inline constexpr std::array<SkillGate, 7> kSkillGates = {{
    {SkillId::Antennae, SkillId::Loadout, 1},
    {SkillId::Reach, SkillId::Loadout, 2},
    {SkillId::SecondChance, SkillId::Health, 3},
    {SkillId::Magnetism, SkillId::Loadout, 5},
    {SkillId::Duplicator, SkillId::Loadout, 4},
    {SkillId::BodyToxicity, SkillId::SharpEdges, 6},
    {SkillId::ConcentratedPoison, SkillId::Poison, 3},
}};

inline const SkillGate* skillGate(SkillId id) {
    for (const SkillGate& gate : kSkillGates) {
        if (gate.child == id) return &gate;
    }
    return nullptr;
}

inline const char* skillTierLabel(SkillId id, int /*tier*/) {
    const std::size_t i = static_cast<std::size_t>(id);
    return i < kSkillLabels.size() ? kSkillLabels[i] : "";
}

// -- effect tables (florrclone client) ----------------------------------------

/// Health: base HP multiplier by owned tier index.
inline constexpr std::array<double, 9> kHealthTalentScale = {
    1.3, 1.69, 2.2, 2.86, 3.71, 4.83, 6.27, 8.16, 10.6,
};
/// Medic / petal healing multiplier.
inline constexpr std::array<double, 9> kMedicTalentScale = {
    1.15, 1.322, 1.521, 1.749, 2.011, 2.313, 2.66, 3.059, 4.046,
};
/// Petal Health multiplier (same curve as medic in florrclone).
inline constexpr std::array<double, 9> kPetalHealthTalentScale = {
    1.15, 1.322, 1.521, 1.749, 2.011, 2.313, 2.66, 3.059, 4.046,
};
/// Reload time reduction fractions; cooldown *= (1 - value).
inline constexpr std::array<double, 9> kReloadReduce = {
    0.1, 0.19, 0.271, 0.344, 0.475, 0.58, 0.664, 0.731, 0.812,
};
/// Rotation speed rad/s.
inline constexpr std::array<double, 5> kRotationRad = {3.1, 3.7, 4.3, 4.9, 5.5};
/// Antennae FOV bonus.
inline constexpr std::array<double, 3> kAntennaeFov = {0.25, 0.5, 1.0};
/// Luck values.
inline constexpr std::array<double, 9> kLuckTalent = {
    0.1, 0.3, 0.5, 0.7, 1.0, 1.4, 1.8, 2.3, 2.8,
};
/// Poison damage bonus fractions.
inline constexpr std::array<double, 9> kPoisonBonus = {
    0.0625, 0.125, 0.1875, 0.25, 0.3125, 0.375, 0.4375, 0.5, 0.5625,
};
/// Reach attack-range bonus.
inline constexpr std::array<double, 3> kReachBonus = {50.0, 100.0, 150.0};
/// Summon health multiplier.
inline constexpr std::array<double, 9> kSummonerScale = {
    1.1, 1.21, 1.331, 1.464, 1.611, 1.772, 1.949, 2.144, 2.594,
};
/// Second Chance: duration / cooldown ms by tier.
inline constexpr std::array<double, 2> kSecondChanceDurationMillis = {300.0, 1500.0};
inline constexpr std::array<double, 2> kSecondChanceCooldownMillis = {60000.0, 30000.0};

inline constexpr int kMaxPetalCopies = 64;
inline constexpr int kDuplicatorMinCopies = 2;
inline constexpr int kBaseLoadoutSlots = 5;
inline constexpr int kMaxLoadoutSlots = 10;

inline double tableAt(const double* table, int size, int tier) {
    return (tier >= 0 && tier < size) ? table[tier] : 1.0;
}

template <std::size_t N>
inline double tableAt(const std::array<double, N>& table, int tier) {
    return (tier >= 0 && tier < static_cast<int>(N)) ? table[static_cast<std::size_t>(tier)] : 1.0;
}

/// Compatibility name used by talent UI helpers.
template <std::size_t N>
inline double scaleAt(const std::array<double, N>& table, int tier) {
    return tableAt(table, tier);
}

inline std::array<double, 2> secondChanceEffect(int tier) {
    if (tier < 0 || tier >= 2) return {0.0, 0.0};
    return {kSecondChanceDurationMillis[static_cast<std::size_t>(tier)],
            kSecondChanceCooldownMillis[static_cast<std::size_t>(tier)]};
}

struct SkillSet {
    /// Highest owned tier index per branch, or -1 if none.
    /// florrclone JSON stores 1-based levels (0 = none); convert on load/save.
    std::array<std::int8_t, kSkillCount> tier{};

    SkillSet() { clear(); }
    void clear() { tier.fill(-1); }

    int level(SkillId id) const { return tier[static_cast<std::size_t>(id)]; }
    void set(SkillId id, int t) { tier[static_cast<std::size_t>(id)] = static_cast<std::int8_t>(t); }

    /// florrclone save uses 0 for none, 1..n for owned level.
    int florrLevel(SkillId id) const {
        const int t = level(id);
        return t < 0 ? 0 : t + 1;
    }
    void setFlorrLevel(SkillId id, int florrLevel) {
        if (florrLevel <= 0) set(id, -1);
        else set(id, florrLevel - 1);
    }

    int spent() const {
        int total = 0;
        for (int s = 0; s < kSkillCount; ++s) {
            const int owned = tier[static_cast<std::size_t>(s)];
            for (int t = 0; t <= owned; ++t) total += skillTierCost(static_cast<SkillId>(s), t);
        }
        return total;
    }

    bool prerequisiteMet(SkillId id) const {
        const SkillGate* gate = skillGate(id);
        if (gate == nullptr) return true;
        return florrLevel(gate->parent) >= gate->parentLevel;
    }

    int loadoutSlots() const {
        const int extra = florrLevel(SkillId::Loadout);
        const int slots = kBaseLoadoutSlots + extra;
        return slots > kMaxLoadoutSlots ? kMaxLoadoutSlots : slots;
    }

    double healthScale() const {
        const int t = level(SkillId::Health);
        return t < 0 ? 1.0 : tableAt(kHealthTalentScale, t);
    }
    double petalHealthScale() const {
        const int t = level(SkillId::PetalHealth);
        return t < 0 ? 1.0 : tableAt(kPetalHealthTalentScale, t);
    }
    double medicScale() const {
        const int t = level(SkillId::Medic);
        return t < 0 ? 1.0 : tableAt(kMedicTalentScale, t);
    }
    /// Cooldown multiplier (≤ 1). Also applies florrclone feel half-reload outside.
    double reloadScale() const {
        const int t = level(SkillId::Reload);
        if (t < 0) return 1.0;
        return 1.0 - tableAt(kReloadReduce, t);
    }
    double rotationSpeed() const {
        const int t = level(SkillId::Rotation);
        return t < 0 ? 2.5 : tableAt(kRotationRad, t);  // base ~2.5 if unknown
    }
    double antennaeFovBonus() const {
        const int t = level(SkillId::Antennae);
        return t < 0 || !prerequisiteMet(SkillId::Antennae) ? 0.0 : tableAt(kAntennaeFov, t);
    }
    double luck() const {
        const int t = level(SkillId::Luck);
        return t < 0 ? 0.0 : tableAt(kLuckTalent, t);
    }
    double poisonBonus() const {
        const int t = level(SkillId::Poison);
        return t < 0 ? 0.0 : tableAt(kPoisonBonus, t);
    }
    double reachBonus() const {
        const int t = level(SkillId::Reach);
        return t < 0 || !prerequisiteMet(SkillId::Reach) ? 0.0 : tableAt(kReachBonus, t);
    }
    double summonerScale() const {
        const int t = level(SkillId::Summoner);
        return t < 0 ? 1.0 : tableAt(kSummonerScale, t);
    }
    /// Body damage × 1.38^(tier+1) when Sharp Edges owned.
    double bodyDamageScale() const {
        const int t = level(SkillId::SharpEdges);
        if (t < 0) return 1.0;
        double m = 1.0;
        for (int i = 0; i <= t; ++i) m *= 1.38;  // 1.38^(t+1)
        return m;
    }
    bool bodyToxicity() const {
        return level(SkillId::BodyToxicity) >= 0 && prerequisiteMet(SkillId::BodyToxicity);
    }
    bool concentratedPoison() const {
        return level(SkillId::ConcentratedPoison) >= 0 &&
               prerequisiteMet(SkillId::ConcentratedPoison);
    }
    bool secondChanceUnlocked() const { return prerequisiteMet(SkillId::SecondChance); }
    bool magnetismUnlocked() const {
        return level(SkillId::Magnetism) >= 0 && prerequisiteMet(SkillId::Magnetism);
    }
    double magnetismBonus() const { return magnetismUnlocked() ? 250.0 : 0.0; }

    /// florrclone duplicator: tiers unlock higher rarity duplication; bonus copies
    /// for multi-petal slots. Conservative: +1 at tier0, +2 at tier1+.
    int petalCopies(int copies) const {
        if (copies < 2 || !prerequisiteMet(SkillId::Duplicator)) return copies;
        const int t = level(SkillId::Duplicator);
        if (t < 0) return copies;
        const int extra = t >= 1 ? 2 : 1;
        const int boosted = copies + extra;
        return boosted < kMaxPetalCopies ? boosted : kMaxPetalCopies;
    }

    // Compatibility shims for call sites still using old flowrix SkillId names
    // via overloaded helpers below — prefer the methods above.
    double healthScale(SkillId id) const {
        if (id == SkillId::PetalHealth) return petalHealthScale();
        if (id == SkillId::Health) return healthScale();
        if (id == SkillId::Summoner) return summonerScale();
        return 1.0;
    }
    double effectScale(SkillId id) const {
        if (id == SkillId::Medic) return medicScale();
        return 1.0;
    }
    double statScale(SkillId id) const {
        if (id == SkillId::Health) return healthScale();
        if (id == SkillId::SharpEdges) return bodyDamageScale();
        return 1.0;
    }
    bool prestigeUnlocked() const { return false; }
};

inline int petalCopies(int copies, const SkillSet* skills) {
    return skills != nullptr ? skills->petalCopies(copies) : copies;
}

inline int availableTalentPoints(int level, const SkillSet& skills, int migrationBonus = 0) {
    const int free = level + migrationBonus - skills.spent();
    return free > 0 ? free : 0;
}

inline SkillId skillFromKey(const std::string& key) {
    for (int i = 0; i < kSkillCount; ++i) {
        if (key == kSkillKeys[static_cast<std::size_t>(i)]) return static_cast<SkillId>(i);
    }
    // Legacy flowrix keys → nearest florrclone branch (import only).
    if (key == "playerHealth" || key == "damage") return SkillId::Health;
    if (key == "healingMultiplier" || key == "healing") return SkillId::Medic;
    if (key == "absorbing" || key == "petHealth") return SkillId::Count;  // drop
    if (key == "prestige") return SkillId::Count;
    return SkillId::Count;
}

struct SkillStep {
    SkillId id = SkillId::Health;
    int tier = 0;
};

inline std::vector<SkillStep> skillPurchasePath(const SkillSet& skills, SkillId id, int tier) {
    std::vector<SkillStep> path;
    if (tier < 0 || tier >= skillTierCount(id) || tier <= skills.level(id)) return path;
    if (const SkillGate* gate = skillGate(id); gate != nullptr && !skills.prerequisiteMet(id)) {
        const int needTier = gate->parentLevel - 1;
        for (int t = skills.level(gate->parent) + 1; t <= needTier; ++t) {
            path.push_back({gate->parent, t});
        }
    }
    for (int t = skills.level(id) + 1; t <= tier; ++t) path.push_back({id, t});
    return path;
}

inline int skillPurchaseCost(const SkillSet& skills, SkillId id, int tier) {
    int total = 0;
    for (const SkillStep& step : skillPurchasePath(skills, id, tier)) {
        total += skillTierCost(step.id, step.tier);
    }
    return total;
}

inline void skillRefundTo(SkillSet& skills, SkillId id, int newLevel) {
    if (newLevel < -1) newLevel = -1;
    if (newLevel >= skillTierCount(id)) newLevel = skillTierCount(id) - 1;
    skills.set(id, newLevel);
    for (const SkillGate& gate : kSkillGates) {
        if (gate.parent != id) continue;
        if (!skills.prerequisiteMet(gate.child)) skills.set(gate.child, -1);
    }
}

// Dead aliases kept so older docs/comments still compile if referenced as ints.
inline constexpr SkillId kSecondChanceParent = SkillId::Health;

}  // namespace flix
