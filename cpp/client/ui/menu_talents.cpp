// Talents panel: florrclone talent tree only.
//
// Layout, camera projection, talent ids/costs and node icons come from
// florrclone (mirrored client + assets/talents/*.svg). The flowrix/reference
// procedural icons and zigZag/spiral tree are not used.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "client/ui/content_i18n.h"
#include "client/ui/talent_icons.h"
#include "svg.h"
#include <memory>
#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "client/render/mob_art.h"
#include "client/ui/text.h"
#include "client/world_view.h"
#include "shared/game/constants.h"
#include "shared/game/prestige.h"

namespace flix {

using namespace flix::ui;

namespace {

// -- the window ---------------------------------------------------------------

constexpr double kWindowSide = 650.0;
constexpr double kWindowX = 95.0;
constexpr double kWindowBottom = 20.0;
/// The tree and the TP badge are clipped to the window less 3 a side.
constexpr double kInset = 3.0;
/// The title: 24 on a 1.4 line, 10 in.
constexpr double kTitleSize = 24.0;
constexpr double kPad = 10.0;
constexpr double kLine = 1.4;
/// The close button, the craft window's: 25 square, 39 in from the right.
constexpr double kCloseSide = 25.0;
constexpr double kCloseFromRight = 39.0;
constexpr double kCloseRimGap = 2.0;
/// Reset: a b4 r4 button 28 tall, 10 in from the bottom-right corner, 14
/// either side of its 16-unit label (plus the label's outline padding).
constexpr double kResetHeight = 28.0;
constexpr double kResetPadX = 14.0;
constexpr double kResetLabelSize = 16.0;
constexpr double kButtonBorder = 4.0;
constexpr double kButtonRadius = 4.0;
constexpr std::uint32_t kResetFace = 0xDE1F1Fu;
constexpr std::uint32_t kResetIdleFace = 0x777777u;

/// The TP badge, inside the clip: a 23-radius disc of the theme's shade
/// centred 40 in, the count 36 on it, and "TP" 24 from 35 past its centre.
/// Hovering the strip they make, 56 tall from 12 in, shows a tooltip.
constexpr double kBadgeCentre = 40.0;
constexpr double kBadgeRadius = 23.0;
constexpr double kBadgeTextSize = 36.0;
constexpr double kBadgeLabelSize = 24.0;
constexpr double kBadgeLabelX = 35.0;
constexpr double kBadgeHitInset = 12.0;
constexpr double kBadgeHitHeight = 56.0;
constexpr double kBadgeHitExtra = 63.0;
constexpr double kBadgeTipY = 78.0;
// Tip text comes from tipMoreTp() so it follows the language toggle.
constexpr double kBadgeTipSize = 16.0;

// -- the tree -----------------------------------------------------------------

/// florrclone talent-wheel stage is an 880×880 viewBox. Nodes are projected
/// with Nv(tier, phase, camera); only |phase−camera|<3 are shown.
constexpr double kWheelSize = 880.0;
constexpr double kCameraMax = 27.0;
constexpr double kPhaseVisible = 3.0;
constexpr double kFlowerX = 440.0;
constexpr double kFlowerY = 777.0;
constexpr double kFlowerRadius = 50.0;
constexpr double kHubRadius = 20.0;
/// Drag across the wheel: same coefficient as florrclone Mv(..., n=0).
constexpr double kDragCamera = 0.0028;
constexpr double kWheelCameraStep = 0.55;

/// A node: a 38.5 disc in its rim colour under a 32.5 one in its fill. The
/// cursor finds it within 32.5, and within 35.75 once it is already found.
constexpr double kNodeRim = 38.5;
constexpr double kNodeFace = 32.5;
constexpr double kHitRadius = 32.5;
constexpr double kHitRadiusHovered = 35.75;
/// What is left to pay, on a 14 disc of the rim colour at (26, -26), 20 high
/// and turned a sixteenth of a half turn.
constexpr double kCostAt = 26.0;
constexpr double kCostRadius = 14.0;
constexpr double kCostSize = 20.0;
constexpr double kCostTurn = kPi / 16.0;

/// Connectors: 8 wide, round, in the theme's shade, kept 10 clear of both
/// ends. Solid into a tier the account owns; otherwise 10-long dashes, one
/// every 20.
constexpr double kLinkWidth = 8.0;
constexpr double kLinkInset = 10.0;
constexpr double kDash = 10.0;
constexpr double kDashPitch = 20.0;

/// Holding a node: it buys after 250 ms and wobbles 10% in size, sin(0.03 t)
/// with t in ms, until the purchase lands. A purchase the server never
/// confirms stops wobbling after a few seconds.
constexpr double kHoldMs = 250.0;
constexpr double kWobbleRate = 0.03;
constexpr double kRequestTimeoutMs = 3000.0;

/// Every ease is x += (target - x)(1 - e^(-k dt)), dt in ms.
constexpr double kScrollRate = 0.00632163;
constexpr double kHoverRate = 0.0133886;
constexpr double kTipRate = 0.0214005;
constexpr double kFlashRate = 0.00435424;

/// A purchase: 40 grains of the tier's colour from the node, and the flower
/// bulging 10 bigger and back as its flash runs out.
constexpr int kBurstGrains = 40;
constexpr double kFlashBulge = 10.0;
constexpr SlotBurstShape kBurstShape{200.0, 0.0, kTau};

constexpr std::uint32_t kLockedFill = 0x777777u;
constexpr std::uint32_t kLockedRim = 0x606060u;
constexpr std::uint32_t kCostShort = 0xFF6666u;
constexpr std::uint32_t kFlowerBody = 0xFBE878u;

/// florr's tooltip box: half-black, corners rounded 5 with quadratics, the
/// text 7 in from either side, 5 from the top and 6 from the bottom. Over a
/// node it stands with its bottom 43.5 above the node's centre; the hint
/// under one has its top 40.5 below. A box never comes closer than 5 to the
/// view's sides.
constexpr double kTipPadX = 7.0;
constexpr double kTipPadTop = 5.0;
constexpr double kTipPadBottom = 6.0;
constexpr double kTipRadius = 5.0;
constexpr double kTipAbove = 43.5;
constexpr double kTipBelow = 40.5;
constexpr double kTipViewMargin = 5.0;
constexpr double kTipNameSize = 24.0;
constexpr double kTipBodySize = 14.0;
/// Between the rarity and the description.
constexpr double kTipGap = 16.0;
/// The badge's tooltip hangs under a point; florr's box top is 7 above it.
constexpr double kTipUnder = 7.0;

/// The two lines beside the flower: 14 high, starting 65 to its right, the
/// first ending level with its centre.
constexpr double kStatSize = 14.0;
constexpr double kStatX = 65.0;

/// Body damage is never surfaced to the client, so the line quotes a fixed
/// base under the tree's multiplier rather than inventing a level curve.
constexpr double kDisplayBodyDamage = 25.0;

// -- the layout ---------------------------------------------------------------

struct GridPoint {
    double x;
    double y;
};


struct Branch {
    SkillId skill;
    std::vector<GridPoint> points;
};

/// Left to right. The long branches wear florr's shapes; what branches OFF
/// them is ours, laid out for our forks:
///   - Flower Health's zig-zag is mirrored so its rare tier stands at the
///     bottom right, and Second Chance climbs away from it toward the
///     middle;
///   - Prestige stands alone just right of the flower;
///   - Reload winds in the spiral (nine tiers, like florr's own);
///   - Healing is four tiers up the start of a zig-zag;
///   - Absorption's zig-zag is mirrored so legendary and mythic both stand
///     on its left edge, a column clear of Healing: Magnetism hooks off
///     legendary to the left and curls down and back in, and Duplicator
///     climbs off mythic up-left above it.
/// Checked across every scroll: no two nodes come closer than 92 (florr's own
/// tree comes to 81), Magnetism's none closer than 116, and no connector
/// crosses another.
const std::vector<Branch>& branches() {
    // Exact florrclone client grid ([tier, phase] → {x,y}). No flowrix zigZag/spiral.
    static const std::vector<Branch> kBranches = {
        {SkillId::PetalHealth,
         {{4, 1}, {5, 0.5}, {4, 0}, {6, 0}, {6, 1}, {8, 1}, {7, 0.5}, {8, 0}, {9, 0.5}}},
        {SkillId::Loadout, {{4, 3.5}, {5, 3}, {6, 3.5}, {7, 3}, {8, 3.5}}},
        {SkillId::Antennae, {{4, 2.5}, {5, 2}, {6, 1.5}}},
        {SkillId::Reach, {{6, 2.5}, {7, 2}, {8, 1.5}}},
        {SkillId::Duplicator, {{8, 2.5}, {9.5, 2.5}, {9, 2}, {9.5, 1.5}}},
        {SkillId::Magnetism, {{9, 4}}},
        {SkillId::Reload,
         {{4, 4.5}, {6, 4}, {7.5, 4}, {8, 4.5}, {8, 5}, {7, 5.5}, {5, 5.5}, {5, 4.5}, {6, 5}}},
        {SkillId::Rotation, {{5, 6}, {7, 6}, {9, 5.5}, {9.5, 5}, {9.5, 4.5}}},
        {SkillId::Health,
         {{4, 7}, {5, 6.5}, {6, 7}, {7, 6.5}, {8.5, 6}, {9, 6.5}, {8, 7}, {8, 7.5}, {9.5, 7}}},
        {SkillId::SecondChance, {{5, 7.5}, {6.5, 7.5}}},
        {SkillId::Medic,
         {{4, 8}, {5, 8.5}, {6, 8}, {8, 8}, {9.5, 7.5}, {9.5, 8}, {9, 8.5}, {7, 8.5}, {6, 9}}},
        {SkillId::Summoner,
         {{4, 9.5}, {5, 10}, {6, 9.5}, {6, 10.5}, {7, 10}, {8, 10.5}, {9, 10}, {8, 9.5}, {9, 9}}},
        {SkillId::SharpEdges,
         {{4, 10.5}, {5, 11}, {6, 11.5}, {7, 11}, {8, 11.5}, {9.5, 11.5}, {9, 11}, {9.5, 10.5}}},
        {SkillId::BodyToxicity, {{9.8, 11}}},
        {SkillId::Luck,
         {{4, 11.5}, {5, 12}, {4, 12.5}, {6, 12.5}, {7, 12}, {8, 12.5}, {9, 13}, {7.5, 13}, {6, 13}}},
        {SkillId::Poison,
         {{4, 14.5}, {5, 14}, {6, 14.5}, {7, 14}, {8, 14.5}, {9, 14}, {9, 15}, {8, 15.5}, {7, 15.5}}},
        {SkillId::ConcentratedPoison, {{6, 15.5}}},
        {SkillId::Damage,
         {{4, 17}, {5, 16.5}, {6, 17}, {7, 16.5}, {8, 17}, {9, 16.5},
          {9.5, 17}, {9, 17.5}, {8, 18}, {7, 18}}},
        {SkillId::Absorbing,
         {{4, 19.5}, {5, 19}, {6, 19.5}, {7, 19}, {8, 19.5}, {9, 19},
          {9.5, 19.5}, {9, 20}, {8, 20.5}, {7, 20.5}}},
        {SkillId::MovementSpeed, {{4, 21.5}, {5.5, 22}, {7, 21.5}}},
        {SkillId::Prestige, {{4, 23.5}}},
        {SkillId::PetDamage,
         {{4, 25}, {5, 24.5}, {6, 25}, {7, 24.5}, {8, 25}, {9, 24.5},
          {9.5, 25}, {9, 25.5}, {8, 26}, {7, 26}}},
    };
    return kBranches;
}

/// Preserve the original opening view when extending the wheel to the right.
constexpr double kStartCamera = 8.25;

/// florrclone Nv: grid.x = tier, grid.y = phase; camera scrolls phase.
Vec2 projectWheel(Vec2 grid, double camera) {
    const double n = grid.y - camera;
    return {440.21856 + 43.37949 * grid.x * n,
            964.36761 - 86.80999 * grid.x + 159.61889 * (1.0 - std::cos(n * 1.184))};
}

Vec2 mapWheel(Vec2 wheel, Rect inner) {
    const double scale = std::min(inner.w, inner.h) / kWheelSize;
    const double ox = inner.x + (inner.w - kWheelSize * scale) * 0.5;
    const double oy = inner.y + (inner.h - kWheelSize * scale) * 0.5;
    return {ox + wheel.x * scale, oy + wheel.y * scale};
}

double ease(double dtMs, double rate) { return 1.0 - std::exp(-rate * dtMs); }

/// A colour's channels scaled by `k` and pulled toward `toward` by the rest.
std::uint32_t mixChannels(std::uint32_t c, double k, double toward) {
    const auto channel = [&](int shift) {
        const double v = static_cast<double>((c >> shift) & 0xFFu) * k + toward * (1.0 - k);
        return static_cast<std::uint32_t>(std::clamp(v, 0.0, 255.0)) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

/// A tier's own colour from its rarity ladder index.
std::uint32_t tierColor(SkillId skill, int tier) {
    return rarityColor(clampRarity(skillTierRarityIndex(skill,tier)));
}

/// florr's labels: white unless told, black outline at 0.12 of the size, round
/// joins, centred on `y`.
TextStyle florrLabel(double size, std::uint32_t fill = kPaper, Align align = Align::Left) {
    TextStyle style;
    style.size = size;
    style.fill = fill;
    style.align = align;
    style.baseline = Baseline::Middle;
    style.roundJoin = true;
    return style;
}

/// The padding florr's text widget adds to a run for its outline.
double outlinePad(double size) {
    return 2.0 * std::floor(2.0 * size * kTextStrokeRatio);
}

// -- the tooltip --------------------------------------------------------------

struct Run {
    std::string text;
    std::uint32_t color = kPaper;
};

struct TipRow {
    double size = kTipBodySize;
    std::vector<Run> runs;
    double gapBefore = 0.0;
};

double rowWidth(const TipRow& row) {
    double width = 0.0;
    for (const Run& run : row.runs) width += measure(run.text, row.size);
    return width;
}

Vec2 tipSize(const std::vector<TipRow>& rows) {
    double width = 0.0;
    double height = kTipPadTop + kTipPadBottom;
    for (const TipRow& row : rows) {
        width = std::max(width, rowWidth(row));
        height += row.gapBefore + row.size * kLine;
    }
    return {width + kTipPadX * 2.0, height};
}

/// The box with its top-left at `at`, at `alpha`: black at half of it, the
/// text at all of it.
void paintTip(Canvas& canvas, Vec2 at, const std::vector<TipRow>& rows, double alpha) {
    const Vec2 size = tipSize(rows);
    const double base = canvas.globalAlpha();
    canvas.save();
    canvas.setGlobalAlpha(static_cast<float>(base * alpha * 0.5));
    fillQuadRound(canvas, {at.x, at.y, size.x, size.y}, kTipRadius, kInk);
    canvas.setGlobalAlpha(static_cast<float>(base * alpha));
    double top = at.y + kTipPadTop;
    for (const TipRow& row : rows) {
        top += row.gapBefore;
        double penX = at.x + kTipPadX;
        const double middle = top + row.size * kLine * 0.5;
        for (const Run& run : row.runs) {
            text(canvas, run.text, penX, middle, florrLabel(row.size, run.color));
            penX += measure(run.text, row.size);
        }
        top += row.size * kLine;
    }
    canvas.restore();
}

/// Where a box of `size` stands centred on `x`, kept inside the view.
double tipLeft(double x, double width, double viewWidth) {
    const double left = x - width * 0.5;
    return std::clamp(left, kTipViewMargin, std::max(kTipViewMargin, viewWidth - kTipViewMargin - width));
}

std::string percent(double fraction) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f%%", fraction * 100.0);
    return buffer;
}

/// "from X% to Y%" over one of the tree's tables: what the tier below gave,
/// or 100% with nothing below, and what this one gives.
template <std::size_t N>
std::string fromTo(const std::array<double, N>& table, int tier) {
    const double before = tier > 0 ? scaleAt(table, tier - 1) : 1.0;
    if (language() == Language::Zh)
        return percent(before) + " → " + percent(scaleAt(table, tier));
    return "from " + percent(before) + " to " + percent(scaleAt(table, tier)) + ".";
}

/// A tier's description, in florr's words for the talent where florr has it
/// and in its manner where it does not: stat names in their stat colours.
std::vector<TipRow> description(SkillId skill, int tier) {
    std::vector<TipRow> rows;
    const auto row = [&](std::vector<Run> runs) { rows.push_back({kTipBodySize, std::move(runs)}); };
    const bool cn = language() == Language::Zh;
    char buffer[128];
    switch (skill) {
        case SkillId::PetDamage:
            row({{cn ? "宠物伤害：" : "Pet damage: "},
                 {fromTo(kPetDamageTalentScale, tier), kStatDamage}});
            row({{cn ? "提升宠物体伤及发射物伤害，不增加数量或攻速。" : "Boosts pet contact and projectile damage, without more pets or faster attacks."}});
            break;
        case SkillId::Prestige:
            row({{cn ? "解锁声望地图：普通怪物提高一个稀有度，最高为超级。" : "Unlock Prestige worlds: ordinary enemies rise one rarity, up to Super."}});
            row({{cn ? "不提高经验档位；重置天赋会退出声望地图。" : "XP tiers stay unchanged; resetting talents returns you to ordinary worlds."}});
            break;
        case SkillId::Damage:
            row({{cn ? "身体伤害：" : "Body damage: "},
                 {fromTo(kDamageTalentBodyScale, tier), kStatDamage}});
            row({{cn ? "花瓣伤害：" : "Petal damage: "},
                 {fromTo(kDamageTalentPetalScale, tier), kStatDamage}});
            break;
        case SkillId::Absorbing:
            row({{cn ? "吸收经验：" : "Absorbed XP: "},
                 {fromTo(kAbsorbTalentScale, tier), kStatHealth}});
            break;
        case SkillId::MovementSpeed:
            row({{cn ? "移动速度：" : "Movement speed: "},
                 {fromTo(kMovementSpeedTalentScale, tier), kStatHealth}});
            break;
        case SkillId::Health:
            if (cn) row({{"提升基础"}, {"花朵生命", kStatHealth}, {" " + fromTo(kHealthTalentScale, tier)}});
            else row({{"Increases base "}, {"Flower Health", kStatHealth},
                      {" " + fromTo(kHealthTalentScale, tier)}});
            break;
        case SkillId::SharpEdges:
            row({{cn ? "仅提升花朵体伤：" : "Body damage only: "},
                 {percent(std::pow(1.38,tier))+" → "+percent(std::pow(1.38,tier+1)),kStatDamage}});
            break;
        case SkillId::PetalHealth:
            if (cn) row({{"提升"}, {"花瓣生命", kStatHealth},
                         {" " + fromTo(kPetalHealthTalentScale, tier)}});
            else row({{"Increases "}, {"Petal Health", kStatHealth},
                      {" " + fromTo(kPetalHealthTalentScale, tier)}});
            break;
        case SkillId::Summoner:
            if (cn) row({{"提升召唤物"}, {"生命", kStatHealth},
                         {" " + fromTo(kSummonerScale, tier)}});
            else row({{"Increases "}, {"Health", kStatHealth},
                      {" of summoned mobs " + fromTo(kSummonerScale, tier)}});
            break;
        case SkillId::Medic:
            if (cn) row({{"提升花瓣"}, {"治疗", kStatHeal},
                         {" " + fromTo(kMedicTalentScale, tier)}});
            else row({{"Increases "}, {"Healing", kStatHeal},
                      {" from your petals " + fromTo(kMedicTalentScale, tier)}});
            break;
        case SkillId::Luck:
            std::snprintf(buffer,sizeof buffer,cn?"幸运：%.2f → %.2f（不增加吸收经验）":"Luck: %.2f → %.2f (does not increase absorb XP)",tier>0?kLuckTalent[tier-1]:0,kLuckTalent[tier]);
            row({{buffer}});break;
        case SkillId::Reload: {
            const double before = tier > 0 ? scaleAt(kReloadReduce, tier - 1) : 0.0;
            const double after = scaleAt(kReloadReduce, tier);
            if (cn) {
                row({{"将花瓣装填时间从 -" + percent(before) + " 降至 -" + percent(after) + "。"}});
            } else {
                row({{"Decreases the reload time for petals from -" + percent(before) + " to -" +
                      percent(after) + "."}});
            }
            SkillSet preview;preview.set(SkillId::Reload,tier);
            std::snprintf(buffer,sizeof buffer,cn?"切换装备最短冷却：%.1f秒":"Minimum equip cooldown: %.1fs",preview.equipReloadFloorMillis()/1000.0);
            row({{buffer}});
            break;
        }
        case SkillId::SecondChance: {
            const auto [duration, cooldown] = secondChanceEffect(tier);
            if (cn) {
                if (tier == 0) {
                    std::snprintf(buffer, sizeof buffer,
                                  "受到致命伤害时免死 %.3g 秒。冷却 %.3g 秒。",
                                  duration / 1000.0, cooldown / 1000.0);
                } else {
                    std::snprintf(buffer, sizeof buffer,
                                  "持续时间增至 %.3g 秒，冷却降至 %.3g 秒。",
                                  duration / 1000.0, cooldown / 1000.0);
                }
            } else if (tier == 0) {
                std::snprintf(buffer, sizeof buffer,
                              "Prevents death for %gs upon taking fatal damage. Has a %gs cooldown.",
                              duration / 1000.0, cooldown / 1000.0);
            } else {
                std::snprintf(buffer, sizeof buffer,
                              "Increases duration to %gs, decreases cooldown to %gs.",
                              duration / 1000.0, cooldown / 1000.0);
            }
            row({{buffer}});
            break;
        }
        case SkillId::Duplicator:
            if(tier<2)row({{cn ? (tier==0?"多粒花瓣额外增加1粒。":"多粒花瓣累计额外增加2粒。") :
                                 (tier==0?"Adds one particle to multi-particle petals.":"Adds two particles to multi-particle petals.")}});
            else row({{cn ? (tier==2?"最左槽可叠加花瓣再增加1粒；不适用于永恒及以上。":"最左槽额外1粒的效果也适用于永恒及以上。") :
                             (tier==2?"Adds one particle to the leftmost stackable petal below Eternal.":"Allows the leftmost extra particle at Eternal and above.")}});
            break;
        case SkillId::Magnetism: {
            if (cn) {
                std::snprintf(buffer, sizeof buffer, "花瓣拾取范围增加 %.0f。",
                              1000.0);
            } else {
                std::snprintf(buffer, sizeof buffer, "Increases petal pick up range by %.0f.",
                              1000.0);
            }
            row({{buffer}});
            break;
        }
        case SkillId::Loadout:
            row({{std::string(cn?"可装备花瓣槽位：":"Active loadout slots: ")+std::to_string(6+tier)}});break;
        case SkillId::Rotation:
            std::snprintf(buffer,sizeof buffer,cn?"基础旋转速度：%.1f rad/s":"Base rotation: %.1f rad/s",kRotationRad[tier]);row({{buffer}});break;
        case SkillId::Antennae:
            row({{std::string(cn?"视野增加：":"Vision increase: ")+percent(kAntennaeFov[tier])}});
            row({{cn?"与触角花瓣取更强效果，不相乘叠加。":"Uses the stronger of this talent and Antennae; does not multiply."}});break;
        case SkillId::Reach:
            row({{std::string(cn?"攻击范围增加：":"Attack reach bonus: ")+std::to_string(static_cast<int>(kReachBonus[tier]))}});break;
        case SkillId::Poison:
            row({{std::string(cn?"剧毒伤害增加：":"Poison damage bonus: ")+percent(kPoisonBonus[tier])}});break;
        case SkillId::BodyToxicity:
            row({{cn?"碰撞额外造成体伤60%的剧毒，持续1秒。":"Contact adds 60% body damage as poison over one second."}});break;
        case SkillId::ConcentratedPoison:
            row({{cn?"剧毒每秒伤害×1.2，持续时间÷1.2，总伤害不变。":"Poison DPS ×1.2 and duration ÷1.2; total damage unchanged."}});break;
        default: break;
    }
    return rows;
}

/// Name, rarity, then the description 16 under them. Prestige has no tier to
/// name, so its box goes straight from the name to the description.
std::vector<TipRow> tooltipRows(SkillId skill, int tier) {
    std::vector<TipRow> rows;
    rows.push_back({kTipNameSize, {{skillTr(skill, tier)}}});
    {
        const Rarity rarity = clampRarity(skillTierRarityIndex(skill,tier));
        rows.push_back({kTipBodySize, {{rarityTr(rarity), rarityColor(rarity)}}});
    }
    std::vector<TipRow> body = description(skill, tier);
    if (!body.empty()) body.front().gapBefore = kTipGap;
    rows.insert(rows.end(), body.begin(), body.end());
    return rows;
}

// -- icons --------------------------------------------------------------------

/// florrclone talent SVGs (assets/talents/*.svg), not flowrix procedural marks.
constexpr double kIconExtent = 35.0 * 0.89;

void drawIcon(Canvas& canvas, SkillId id, int /*tier*/, Vec2 at, double size) {
    const char* svg = talentSvg(id == SkillId::MovementSpeed ? SkillId::Rotation : id);
    if (id == SkillId::PetDamage) svg = talentSvg(SkillId::Summoner);
    if (id == SkillId::Prestige) svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 80 80'><path d='M40 5L50 27L75 30L57 47L61 73L40 61L19 73L23 47L5 30L30 27Z' fill='#ffd43b' stroke='#8e7019' stroke-width='4'/><circle cx='34' cy='40' r='3' fill='#735c18'/><circle cx='46' cy='40' r='3' fill='#735c18'/><path d='M33 48Q40 55 47 48' fill='none' stroke='#735c18' stroke-width='3'/></svg>";
    if (svg == nullptr || *svg == '\0') return;
    static std::array<std::shared_ptr<SvgDocument>, kSkillCount> cache{};
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= cache.size()) return;
    if (!cache[idx]) {
        cache[idx] = std::make_shared<SvgDocument>(SvgDocument::fromString(svg));
    }
    const SvgDocument& doc = *cache[idx];
    if (doc.empty()) return;
    const double half = size * 0.5;
    doc.renderFitted(canvas, static_cast<float>(at.x - half), static_cast<float>(at.y - half),
                     static_cast<float>(size), static_cast<float>(size), 0.0f);
}

// -- the flower ---------------------------------------------------------------

/// The flower at the root of the tree, its pupils turned toward the cursor.
/// Drawn in the 25-unit face space the world renderer uses.
void drawAvatar(Canvas& canvas, Vec2 at, double radius, Vec2 eye) {
    const auto eyeX = static_cast<float>(eye.x);
    const auto eyeY = static_cast<float>(eye.y);
    constexpr float kTauF = static_cast<float>(kTau);

    canvas.save();
    canvas.translate(static_cast<float>(at.x), static_cast<float>(at.y));
    canvas.scale(static_cast<float>(radius / 25.0), static_cast<float>(radius / 25.0));

    setFill(canvas, shade(kFlowerBody, 0.8));
    canvas.beginPath();
    canvas.arc(0, 0, 26.5f, 0, kTauF);
    canvas.fill();
    setFill(canvas, kFlowerBody);
    canvas.beginPath();
    canvas.arc(0, 0, 23.5f, 0, kTauF);
    canvas.fill();

    canvas.save();
    setFill(canvas, kInk);
    canvas.beginPath();
    canvas.ellipse(-7, -4.8f, 3.2f, 6.5f, 0, 0, kTauF);
    canvas.moveTo(10.2f, -4.8f);
    canvas.ellipse(7, -4.8f, 3.2f, 6.5f, 0, 0, kTauF);
    canvas.fill();
    // Pupils and rim are drawn clipped to the eyes, so only the inner half of
    // the outline shows and a pupil at the edge is cut off.
    canvas.clip();
    setFill(canvas, kPaper);
    canvas.beginPath();
    canvas.arc(-7 + eyeX, -4.8f + eyeY, 3, 0, kTauF);
    canvas.arc(7 + eyeX, -4.8f + eyeY, 3, 0, kTauF);
    canvas.fill();
    setStroke(canvas, kInk);
    canvas.setLineWidth(1.0f);
    canvas.beginPath();
    canvas.ellipse(-7, -4.8f, 3.2f, 6.5f, 0, 0, kTauF);
    canvas.stroke();
    canvas.beginPath();
    canvas.ellipse(7, -4.8f, 3.2f, 6.5f, 0, 0, kTauF);
    canvas.stroke();
    canvas.restore();

    setStroke(canvas, 0x222222u);
    canvas.setLineWidth(1.5f);
    canvas.setLineCap("round");
    canvas.beginPath();
    canvas.moveTo(-6, 10);
    canvas.quadraticCurveTo(0, 14.5f, 6, 10);
    canvas.stroke();
    canvas.restore();
}

/// The stat lines' abbreviation: lower-case, and it stops at millions.
std::string abbreviateStat(double value) {
    const double n = std::round(value);
    char buffer[32];
    if (n < 1000.0) {
        std::snprintf(buffer, sizeof buffer, "%.0f", n);
        return buffer;
    }
    const bool millions = n >= 1e6;
    const double scaled = n / (millions ? 1e6 : 1e3);
    std::snprintf(buffer, sizeof buffer, scaled == std::floor(scaled) ? "%.0f%s" : "%.1f%s", scaled,
                  millions ? "m" : "k");
    return buffer;
}

/// A connector from `from` to `to`, 10 clear of each end; dashed from `from`.
void drawLink(Canvas& canvas, Vec2 from, Vec2 to, bool dashed) {
    const double length = distance(from, to);
    if (length <= kLinkInset * 2.0) return;
    const Vec2 along = (to - from) * (1.0 / length);
    const Vec2 a = from + along * kLinkInset;
    const double run = length - kLinkInset * 2.0;
    canvas.beginPath();
    if (!dashed) {
        const Vec2 b = a + along * run;
        canvas.moveTo(static_cast<float>(a.x), static_cast<float>(a.y));
        canvas.lineTo(static_cast<float>(b.x), static_cast<float>(b.y));
    } else {
        for (double d = 0.0; d <= run; d += kDashPitch) {
            const Vec2 p = a + along * d;
            const Vec2 q = a + along * (d + kDash);
            canvas.moveTo(static_cast<float>(p.x), static_cast<float>(p.y));
            canvas.lineTo(static_cast<float>(q.x), static_cast<float>(q.y));
        }
    }
    canvas.stroke();
}

} // namespace

Rect TalentsPanel::bounds(int, int viewHeight) {
    return {kWindowX, static_cast<double>(viewHeight) - kWindowBottom - kWindowSide, kWindowSide,
            kWindowSide};
}

void TalentsPanel::reset() {
    if (!laidOut_) layout();
    dragging_ = false;
    pressedNode_ = -1;
    hoveredNode_ = -1;
    holdMs_ = 0;
    holdBought_ = false;
    badgeTip_ = 0;
    flash_ = 0;
    closeHeld_ = false;
    resetHeld_ = false;
    confirmingReset_ = false;
    for (Node& node : nodes_) {
        node.hover = node.tooltip = node.hint = node.wobble = node.pulse = 0;
        node.requested = false;
        node.refunding = false;
    }
    grains_.clear();
    // The scroll is kept from one opening to the next, as florr keeps it.
    if (!scrollSet_) {
        scroll_ = scrollTarget_ = kStartCamera;
        scrollSet_ = true;
    }
}

void TalentsPanel::layout() {
    nodes_.clear();
    maxColumn_ = kCameraMax;
    for (const Branch& branch : branches()) {
        const int tiers = skillTierCount(branch.skill);
        for (int tier = 0; tier < tiers; ++tier) {
            GridPoint point{0, 0};
            if (tier < static_cast<int>(branch.points.size())) {
                point = branch.points[static_cast<std::size_t>(tier)];
            }
            Node node;
            node.skill = branch.skill;
            node.tier = tier;
            // grid.x = florrclone tier, grid.y = phase
            node.grid = {point.x, point.y};
            nodes_.push_back(node);
        }
    }
    laidOut_ = true;
}

bool TalentsPanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Profile& profile = ctx.net.profile();
    const SkillSet& skills = profile.skills;
    const int points = profile.talentPoints();
    const Rect panel = ctx.bounds;
    const double dtMs = ctx.dt * 1000.0;
    const Vec2 mouse = ctx.mouse();
    if (!laidOut_ || !scrollSet_) reset();

    // florr kills the window's input below 0.9 open and on the way out.
    const bool live = open_ >= 0.9 && !closing_;

    const Rect inner{panel.x + kInset, panel.y + kInset, panel.w - kInset * 2.0,
                     panel.h - kInset * 2.0};
    const Vec2 flower = mapWheel({kFlowerX, kFlowerY}, inner);

    // -- the chrome's rects ---------------------------------------------------
    const double rowMiddle = panel.y + kPad + kTitleSize * kLine * 0.5;
    const Rect closeSquare{panel.right() - kCloseFromRight, rowMiddle - kCloseSide * 0.5,
                           kCloseSide, kCloseSide};
    const Rect closeHit{closeSquare.x - kCloseRimGap, closeSquare.y - kCloseRimGap,
                        kCloseSide + kCloseRimGap * 2.0, kCloseSide + kCloseRimGap * 2.0};
    const double resetWidth =
        measure(tipReset(), kResetLabelSize) + outlinePad(kResetLabelSize) + kResetPadX * 2.0;
    const Rect resetRect{panel.right() - kPad - resetWidth, panel.bottom() - kPad - kResetHeight,
                         resetWidth, kResetHeight};
    const Rect resetHit{resetRect.x - kButtonBorder * 0.5, resetRect.y - kButtonBorder * 0.5,
                        resetRect.w + kButtonBorder, resetRect.h + kButtonBorder};
    const std::string pointsText = std::to_string(points);
    const Rect badgeHit{inner.x + kBadgeHitInset, inner.y + kBadgeHitInset,
                        measure(pointsText, kBadgeTextSize) + outlinePad(kBadgeTextSize) +
                            kBadgeHitExtra,
                        kBadgeHitHeight};
    bool anyOwned = false;
    for (int s = 0; s < kSkillCount; ++s) anyOwned |= skills.level(static_cast<SkillId>(s)) >= 0;

    const bool overClose = live && closeHit.contains(mouse);
    const bool overReset = live && anyOwned && resetHit.contains(mouse);
    if (!overReset) confirmingReset_ = false;

    // -- camera (florrclone phase scroll) -------------------------------------
    if (dragging_) {
        scrollTarget_ += (dragLastX_ - mouse.x) * kDragCamera;
        dragLastX_ = mouse.x;
    }
    if (live && panel.contains(mouse)) scrollTarget_ -= ctx.wheel() * kWheelCameraStep;
    scrollTarget_ = std::clamp(scrollTarget_, 0.0, kCameraMax);
    scroll_ += (scrollTarget_ - scroll_) * ease(dtMs, kScrollRate);

    const auto visible = [&](const Node& node) {
        return std::abs(node.grid.y - scroll_) < kPhaseVisible;
    };
    for (Node& node : nodes_) node.at = mapWheel(projectWheel(node.grid, scroll_), inner);

    // -- what each node is ----------------------------------------------------
    const auto owned = [&](const Node& node) {
        return skills.prerequisiteMet(node.skill) && node.tier <= skills.level(node.skill);
    };
    const auto costOf = [&](const Node& node) {
        return skillPurchaseCost(skills, node.skill, node.tier);
    };
    const auto affordable = [&](const Node& node) {
        return !owned(node) && costOf(node) <= points;
    };

    // Exactly one node answers the cursor: the last drawn, which is the one
    // on top, and never through the chrome drawn over the tree.
    int hovered = -1;
    if (live && !dragging_ && inner.contains(mouse) && !closeHit.contains(mouse) &&
        !resetHit.contains(mouse)) {
        for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; --i) {
            const Node& node = nodes_[static_cast<std::size_t>(i)];
            if (!visible(node)) continue;
            const double reach = i == hoveredNode_ ? kHitRadiusHovered : kHitRadius;
            if (distance(mouse, node.at) <= reach) {
                hovered = i;
                break;
            }
        }
    }
    hoveredNode_ = hovered;

    // -- input ----------------------------------------------------------------
    bool keepOpen = true;
    if (live && ctx.pressed() && panel.contains(mouse)) {
        if (overClose) {
            closeHeld_ = true;
        } else if (overReset) {
            resetHeld_ = true;
        } else if (hovered >= 0) {
            pressedNode_ = hovered;
            holdMs_ = 0;
            holdBought_ = false;
        } else if (inner.contains(mouse)) {
            dragging_ = true;
            dragLastX_ = mouse.x;
        }
    }
    const bool down = ctx.window.mouseDown(MouseButton::Left);
    // A hold lasts while the press stays on its node and the node can still
    // be bought OR refunded; it fires once.
    const bool holdingBuy =
        down && pressedNode_ >= 0 && pressedNode_ == hovered &&
        affordable(nodes_[static_cast<std::size_t>(pressedNode_)]);
    const bool holdingRefund =
        down && pressedNode_ >= 0 && pressedNode_ == hovered &&
        owned(nodes_[static_cast<std::size_t>(pressedNode_)]);
    const bool holding = holdingBuy || holdingRefund;
    if (holding) {
        holdMs_ += dtMs;
        Node& node = nodes_[static_cast<std::size_t>(pressedNode_)];
        if (holdMs_ > kHoldMs && !holdBought_) {
            holdBought_ = true;
            if (holdingRefund) {
                ctx.net.requestDowngradeSkill(node.skill, node.tier);
                node.refunding = true;
            } else {
                for (const SkillStep& step : skillPurchasePath(skills, node.skill, node.tier)) {
                    ctx.net.requestUpgradeSkill(step.id, step.tier);
                }
                node.refunding = false;
            }
            node.requested = true;
            node.requestedMs = holdMs_;
        }
    }
    if (!down) {
        if (closeHeld_ && overClose) keepOpen = false;
        if (resetHeld_ && overReset) {
            // Two clicks, because a reset cannot be undone and there is no
            // modal here to raise. The caption never changes.
            if (confirmingReset_) {
                ctx.net.requestResetSkills();
                confirmingReset_ = false;
            } else {
                confirmingReset_ = true;
            }
        }
        closeHeld_ = resetHeld_ = false;
        dragging_ = false;
        pressedNode_ = -1;
    }

    // -- the nodes' eases -----------------------------------------------------
    const double hoverStep = ease(dtMs, kHoverRate);
    const double tipStep = ease(dtMs, kTipRate);
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        Node& node = nodes_[i];
        const bool isHovered = static_cast<int>(i) == hovered;
        const bool isHeld = holding && static_cast<int>(i) == pressedNode_;
        const bool canBuy = affordable(node);
        const bool canRefund = owned(node);
        const bool landed = node.requested && (node.refunding ? !owned(node) : owned(node));
        if (landed) {
            // Purchase or refund landed: a pulse, the flower's bulge and a burst.
            node.requested = false;
            node.refunding = false;
            node.pulse = 1.0;
            flash_ = 1.0;
            grains_.burst(node.at, tierColor(node.skill, node.tier), kBurstGrains, kBurstShape);
        }
        if (node.requested && !isHeld) {
            node.requestedMs += dtMs;
            if (node.requestedMs > kRequestTimeoutMs) {
                node.requested = false;
                node.refunding = false;
            }
        }
        if (isHeld || node.requested) {
            node.wobble = std::sin(kWobbleRate * (isHeld ? holdMs_ : node.requestedMs));
        } else {
            node.wobble -= node.wobble * hoverStep;
        }
        node.pulse -= node.pulse * hoverStep;
        node.hover +=
            ((isHovered && (canBuy || canRefund) && !isHeld ? 1.0 : 0.0) - node.hover) * hoverStep;
        node.tooltip += ((isHovered && !isHeld ? 1.0 : 0.0) - node.tooltip) * tipStep;
        node.hint +=
            ((isHovered && (canBuy || canRefund) ? 1.0 : 0.0) - node.hint) * tipStep;
    }
    flash_ -= flash_ * ease(dtMs, kFlashRate);
    badgeTip_ += ((live && badgeHit.contains(mouse) ? 1.0 : 0.0) - badgeTip_) * tipStep;

    // -- paint ----------------------------------------------------------------
    canvas.save();
    // The whole window fades in with its open progress, florr's way.
    canvas.setGlobalAlpha(static_cast<float>(std::clamp(open_, 0.0, 1.0)));
    drawFlorrPanel(canvas, panel, kTalentsSkin.fill, kTalentsSkin.border);

    canvas.save();
    canvas.beginPath();
    canvas.rect(static_cast<float>(inner.x), static_cast<float>(inner.y), static_cast<float>(inner.w),
                static_cast<float>(inner.h));
    canvas.clip();

    // The TP badge.
    const Vec2 badge{inner.x + kBadgeCentre, inner.y + kBadgeCentre};
    setFill(canvas, kTalentsSkin.border);
    canvas.fillCircle(static_cast<float>(badge.x), static_cast<float>(badge.y),
                      static_cast<float>(kBadgeRadius));
    text(canvas, pointsText, badge.x, badge.y, florrLabel(kBadgeTextSize, kPaper, Align::Centre));
    text(canvas, "TP", badge.x + kBadgeLabelX, badge.y, florrLabel(kBadgeLabelSize));

    // The connectors, all under the nodes: each tier from the one below it,
    // a fork's first from the node it hangs off, a root from the flower.
    setStroke(canvas, kTalentsSkin.border);
    canvas.setLineWidth(static_cast<float>(kLinkWidth));
    canvas.setLineCap("round");
    canvas.setLineJoin("round");
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        if (!visible(node)) continue;
        const bool dashed = !owned(node);
        if (node.tier > 0) {
            const Node& prev = nodes_[i - 1];
            if (visible(prev)) drawLink(canvas, prev.at, node.at, dashed);
            continue;
        }
        if (const SkillGate* fork = skillGate(node.skill)) {
            for (const Node& parent : nodes_) {
                if (parent.skill == fork->parent && parent.tier == fork->parentLevel - 1) {
                    if (visible(parent)) drawLink(canvas, parent.at, node.at, dashed);
                    break;
                }
            }
            continue;
        }
        drawLink(canvas, node.at, flower, dashed);
    }

    // The flower on its hub, bulging after a purchase.
    setFill(canvas, kTalentsSkin.border);
    canvas.fillCircle(static_cast<float>(flower.x), static_cast<float>(flower.y),
                      static_cast<float>(kHubRadius));
    const double bulge = flash_ > 1e-4 ? kFlashBulge * std::sin(kPi * (1.0 - flash_)) : 0.0;
    const double look = std::atan2(mouse.y - flower.y, mouse.x - flower.x);
    drawAvatar(canvas, flower, kFlowerRadius + bulge,
               {std::cos(look) * kFlowerEyeTravelX, std::sin(look) * kFlowerEyeTravelY});
    grains_.drawGrains(canvas, dtMs);

    // The nodes (only the phase window florrclone shows).
    for (const Node& node : nodes_) {
        if (!visible(node)) continue;
        const bool isOwned = owned(node);
        const int cost = isOwned ? 0 : costOf(node);
        const bool canBuy = !isOwned && cost <= points;
        const std::uint32_t own = tierColor(node.skill, node.tier);
        std::uint32_t fill = kLockedFill;
        std::uint32_t rim = kLockedRim;
        if (isOwned) {
            fill = own;
            rim = slotCardShade(own);
        } else if (canBuy) {
            // Within reach: 30% of the tier's colour over the locked grey.
            fill = mixChannels(own, 0.3, static_cast<double>(kLockedFill & 0xFFu));
            rim = slotCardShade(fill);
        }
        const double pulse = node.pulse > 1e-4 ? 1.0 + 0.5 * std::sin(kPi * node.pulse) : 1.0;
        const double scale = (1.0 + 0.1 * node.hover) * (1.0 + 0.1 * node.wobble) * pulse;

        canvas.save();
        canvas.translate(static_cast<float>(node.at.x), static_cast<float>(node.at.y));
        canvas.scale(static_cast<float>(scale), static_cast<float>(scale));
        setFill(canvas, rim);
        canvas.fillCircle(0.0f, 0.0f, static_cast<float>(kNodeRim));
        setFill(canvas, fill);
        canvas.fillCircle(0.0f, 0.0f, static_cast<float>(kNodeFace));
        drawIcon(canvas, node.skill, node.tier, {0.0, 0.0}, kIconExtent);
        if (!isOwned) {
            setFill(canvas, rim);
            canvas.fillCircle(static_cast<float>(kCostAt), static_cast<float>(-kCostAt),
                              static_cast<float>(kCostRadius));
            canvas.save();
            canvas.translate(static_cast<float>(kCostAt), static_cast<float>(-kCostAt));
            canvas.rotate(static_cast<float>(kCostTurn));
            text(canvas, std::to_string(cost), 0.0, 0.0,
                 florrLabel(kCostSize, canBuy ? kPaper : kCostShort, Align::Centre));
            canvas.restore();
        }
        canvas.restore();
    }


    canvas.restore();  // the tree's clip

    // Reset, the title and the close button, over the tree.
    drawFlorrButton(canvas, resetRect, anyOwned ? kResetFace : kResetIdleFace, kButtonBorder,
                    kButtonRadius, overReset, overReset && resetHeld_);
    text(canvas, tipReset(), resetRect.x + resetRect.w * 0.5, resetRect.y + resetRect.h * 0.5,
         florrLabel(kResetLabelSize, kPaper, Align::Centre));
    text(canvas, menuTitle(MenuId::Talents), panel.x + panel.w * 0.5, rowMiddle,
         florrLabel(kTitleSize, kPaper, Align::Centre));
    drawFlorrClose(canvas, closeSquare, overClose, overClose && closeHeld_);
    // florr paints the rim a second time, so nothing inside overruns it.
    drawFlorrRim(canvas, panel, kTalentsSkin.border);

    // -- tooltips, over everything ---------------------------------------------
    const double viewWidth = static_cast<double>(canvas.width());
    if (badgeTip_ > 1e-3) {
        const std::vector<TipRow> rows = {{kBadgeTipSize, {{tipMoreTp()}}}};
        const Vec2 size = tipSize(rows);
        const double anchorY = inner.y + kBadgeTipY;
        paintTip(canvas, {tipLeft(badge.x, size.x, viewWidth), anchorY - kTipUnder}, rows, badgeTip_);
    }
    for (const Node& node : nodes_) {
        if (node.tooltip > 1e-3) {
            const std::vector<TipRow> rows = tooltipRows(node.skill, node.tier);
            const Vec2 size = tipSize(rows);
            paintTip(canvas,
                     {tipLeft(node.at.x, size.x, viewWidth), node.at.y - kTipAbove - size.y},
                     rows, node.tooltip);
        }
        if (node.hint > 1e-3) {
            const char* holdTip = owned(node) ? tipHoldRefund() : tipHoldUpgrade();
            const std::vector<TipRow> rows = {{kTipBodySize, {{holdTip}}}};
            const Vec2 size = tipSize(rows);
            paintTip(canvas, {tipLeft(node.at.x, size.x, viewWidth), node.at.y + kTipBelow}, rows,
                     node.hint);
        }
    }
    canvas.restore();  // the window's fade
    return keepOpen;
}

} // namespace flix
