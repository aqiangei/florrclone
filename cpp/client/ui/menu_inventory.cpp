#include "shared/game/hyper_petal_techniques.h"
#include "shared/game/computer_set.h"
#include "shared/game/monstera.h"
#include "shared/game/computer_set.h"
// The petal inventory.
//
// The screen players spend the most time in, so the muscle memory is real and
// the layout is held to the reference shot: a five-column grid of 60px cells
// grouped under a centred tier label with a rule running out to either edge of
// the grid, a Stack toggle and a search box above them, and a scroll thumb in
// the strip the grid leaves free on the right.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "client/ease.h"
#include "client/ui/content_i18n.h"
#include "client/ui/item_tile.h"
#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "client/ui/text.h"
#include "client/ui/text_input.h"
#include "shared/core/text.h"
#include "shared/game/config.h"
#include "shared/game/dark_mark.h"
#include "shared/game/domino.h"
#include "shared/game/rarity.h"

namespace flix {

using namespace flix::ui;

namespace {

/// The stack toggle's knob: a quarter of the way per 60 fps frame, the
/// browser's rate.
const double kStackToggleEaseSeconds = easeTimeConstant(0.25);

/// The grid's margins. NOT equal: the right one also has to clear the scroll
/// thumb, so the five columns sit a touch left of the panel's own centre and
/// the thumb rides in the strip they leave. Centring the block on the panel
/// instead puts the last column under the thumb.
constexpr double kGridPadding = 26.0;
constexpr double kGridGutter = 42.0;
/// The air above the first heading and below the last row. Its own constant
/// rather than kGridPadding: the side margins are set by the scroll thumb and
/// this one is not.
constexpr double kGridTopPad = 12.0;
constexpr double kSectionGap = 14.0;
/// The band a tier heading occupies, and how far the tiles under it clear its
/// baseline. Both grew with the cells: a 60px tile needs more air above it
/// than a 56px one before the heading stops looking stuck to it.
constexpr double kLabelHeight = 26.0;
constexpr double kLabelToGrid = 12.0;
constexpr int kColumns = 5;

/// The controls row and everything above it. Fixed, because the grid below
/// scrolls and the header must not.
constexpr double kHeaderHeight = 137.0;
constexpr double kControlsY = 91.0;
constexpr double kControlsHeight = 33.0;
constexpr double kToggleBoxSize = 29.0;
constexpr double kToggleHitWidth = 95.0;

/// The controls row's own inset, wider than `kMenuPadding`: the toggle lines
/// up with the grid's left margin rather than with the title's, so the header
/// and the cells under it share one left edge.
constexpr double kControlsPad = kGridPadding;
constexpr double kControlsRightPad = 22.0;
/// The word beside the toggle, and the tier heading over each run of cells.
/// Both sized to the taller controls and larger cells around them.
constexpr double kToggleLabelSize = 16.0;
constexpr double kSectionLabelSize = 15.0;

/// The height the scroll range, the cull and the scrollbar are all measured
/// against. Deliberately the panel's own bottom padding rather than the
/// kMenuBorder the content is CLIPPED to: the wider strip is reserved, so the
/// last few units of the clip rect are never scrolled into.
///
/// Sized past a single cell so a trailing row that would otherwise sit flush
/// against the panel's floor -- and read as stacked on the row above -- is
/// pushed into the scroll range instead. Smaller than this and a short bag
/// still fits without a thumb; at one cell the thumb appears as soon as a
/// second row would crowd the bottom.
constexpr double kViewBottomPad = kCellSize + kCellGap;

/// One wheel notch is a raw ~100px deltaY in the browser; SDL reports ±1, so
/// the step is spelled out here.
constexpr double kWheelStep = 100.0;

/// The thumb, and how far in from the panel's right edge its RIGHT side sits.
/// There is no track: the panel paints the thumb alone, in the skin's accent
/// so it reads as part of the frame rather than as content.
constexpr double kThumbWidth = 8.0;
constexpr double kThumbInset = 14.0;
constexpr double kThumbMinHeight = 20.0;

/// Long enough that no player reaches it. The field scrolls its own head out
/// of view rather than refusing keys, exactly as the DOM input does.
constexpr std::size_t kSearchLimit = 128;

/// The tier heading's rules: their ink, and the air they leave either side of
/// the word. Round-capped, so the gap reads as a gap and not as a cut.
constexpr double kRuleWidth = 5.0;
constexpr double kRuleGap = 13.0;

/// How wide the hover tooltip's body text wraps. Its dwell before it appears
/// is TooltipDelay::kDelaySeconds.
constexpr double kTooltipWrapWidth = 230.0;

/// One laid-out cell. In stacked mode a cell stands for a petal TYPE at the
/// best tier the account holds, rather than for one stack.
struct Cell {
    Rect rect;
    std::uint16_t petalIndex = kNoPetal;
    Rarity rarity = Rarity::Common;
    std::uint32_t count = 0;
};

struct Section {
    Rarity rarity = Rarity::Common;
    double labelY = 0;
};

struct Grid {
    std::vector<Cell> cells;
    std::vector<Section> sections;
    double contentHeight = 0;
};

/// The tooltip's dwell timer.
///
/// File-local rather than a panel field because InventoryPanel's members are
/// declared in menus.h, which this file does not own -- and there is exactly
/// one inventory panel in the client, so one timer is the right number.
TooltipDelay tooltipDelay;
TouchInspectDelay touchInspect;

bool matchesSearch(std::uint16_t petalIndex, const std::string& needle) {
    if (needle.empty()) return true;
    const PetalConfig& config = content().petal(petalIndex);
    return lowerCase(titleCase(config.name)).find(needle) != std::string::npos ||
           lowerCase(config.id).find(needle) != std::string::npos ||
           std::string(petalNameZh(config.id)).find(needle) != std::string::npos;
}

/// Centre each row within the scrolling content area, including partial rows.
void layoutRun(Grid& grid, const std::vector<Cell>& entries, double left, double innerWidth,
               double& y) {
    if (entries.empty()) return;
    const int rows = static_cast<int>((entries.size() + kColumns - 1) / kColumns);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const int row = static_cast<int>(i) / kColumns;
        const int column = static_cast<int>(i) % kColumns;
        const int rowCount = std::min(kColumns, static_cast<int>(entries.size()) - row * kColumns);
        const double rowWidth = rowCount * kCellSize + (rowCount - 1) * kCellGap;
        const double rowStart = left + (innerWidth - rowWidth) * 0.5;
        Cell cell = entries[i];
        cell.rect = {rowStart + column * (kCellSize + kCellGap), y + row * (kCellSize + kCellGap),
                     kCellSize, kCellSize};
        grid.cells.push_back(cell);
    }
    y += rows * kCellSize + (rows - 1) * kCellGap;
}

/// Builds the grid in CONTENT space: y = 0 is the top of the scrolling area,
/// so the scroll offset is applied once, when drawing, and never leaks into
/// the layout or the hit test.
///
/// Petals only, and not because the panel is unfinished: Profile::Stack
/// carries a petal index alone. Mobs drop no consumables, and the TypeScript
/// build's speed boosts, health potions and shields became powder, roses and
/// shells where an account loads (server/db.cpp).
Grid buildGrid(const Profile& profile, double innerWidth, bool stacked,
               const std::string& needle) {
    Grid grid;
    double y = kGridTopPad;

    if (stacked) {
        // One cell per petal TYPE, shown at the best tier the account holds.
        // Deliberately no tier headings: the point of this mode is a compact
        // catalogue of what you own, not where it sits on the ladder. Petal
        // indices are assigned in sorted-id order, which is the canonical
        // ordering this mode is sorted by in the browser too.
        std::vector<Cell> entries;
        for (std::size_t i = 0; i < content().petalCount(); ++i) {
            const auto petalIndex = static_cast<std::uint16_t>(i);
            if (!matchesSearch(petalIndex, needle)) continue;
            for (int tier = kRarityCount - 1; tier >= 0; --tier) {
                const Rarity rarity = clampRarity(tier);
                const std::uint32_t count = profile.stackCount(petalIndex, rarity);
                if (count == 0) continue;
                entries.push_back({Rect{}, petalIndex, rarity, count});
                break;
            }
        }
        layoutRun(grid, entries, kGridPadding, innerWidth, y);
        y += kSectionGap;
        grid.contentHeight = y + kGridTopPad;
        return grid;
    }

    for (int tier = kRarityCount - 1; tier >= 0; --tier) {
        const Rarity rarity = clampRarity(tier);
        std::vector<Cell> entries;
        // Left in the profile's own order, which is the order the server sent
        // and therefore acquisition order. Sorting it here would arrange the
        // same account's grid differently from the browser's.
        for (const Profile::Stack& stack : profile.inventory) {
            if (stack.rarity != rarity || stack.count == 0) continue;
            if (!matchesSearch(stack.petalIndex, needle)) continue;
            entries.push_back({Rect{}, stack.petalIndex, stack.rarity, stack.count});
        }
        if (entries.empty()) continue;

        y += kLabelHeight;
        grid.sections.push_back({rarity, y - kLabelToGrid});
        layoutRun(grid, entries, kGridPadding, innerWidth, y);
        y += kSectionGap;
    }

    grid.contentHeight = y + kGridTopPad;
    return grid;
}

/// Hit / paint box of the thumb strip: right-inset column over the grid view.
Rect thumbTrack(Rect panel, double contentTop, double visibleH) {
    return {panel.right() - kThumbInset - kThumbWidth, contentTop, kThumbWidth, visibleH};
}

/// The scroll thumb, and only the thumb: a square-cornered quarter-black bar
/// inset from the panel's right edge.
///
/// Local rather than ui::scrollbar(), whose rounded, tracked, opaque bar is
/// what the other panels are drawn with. This one has no track at all.
void scrollThumb(Canvas& canvas, Rect panel, double contentTop, double visibleH,
                 double contentHeight, double scroll) {
    if (contentHeight <= visibleH || visibleH <= 0) return;
    const double travel = contentHeight - visibleH;
    const double thumbHeight = std::max(kThumbMinHeight, visibleH * (visibleH / contentHeight));
    const double thumbY = contentTop + clamp(scroll / travel, 0.0, 1.0) * (visibleH - thumbHeight);
    setFill(canvas, kInventorySkin.accent);
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(panel.right() - kThumbInset - kThumbWidth),
                     static_cast<float>(thumbY), static_cast<float>(kThumbWidth),
                     static_cast<float>(thumbHeight), static_cast<float>(kThumbWidth * 0.5));
    canvas.fill();
}

/// Scrollbar drag state.
///
/// Belongs on InventoryPanel, but menus.h is not this change's to edit.
/// One inventory panel in the process, so a file-local record is the same
/// state under another name (gallery / leaderboard do the same).
struct ThumbDrag {
    bool active = false;
    double startY = 0;
    double startOffset = 0;
};

ThumbDrag& thumbDrag() {
    static ThumbDrag drag;
    return drag;
}

/// A whole number with no separators, for the ALT variant of a stat row. The
/// browser prints the raw value there; commas would be a second abbreviation.
std::string exactNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.0f", value);
    return buffer;
}

/// A fraction as a percentage to a tenth, the tenth dropped when it is zero:
/// a golden leaf's 17.5% would round to a figure florr never prints.
std::string percentNumber(double fraction) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", std::round(fraction * 1000.0) / 10.0);
    return buffer;
}

/// One decimal, dropped when it is zero: a blueberry's 1.2 mana would read as
/// 1 through abbreviate().
std::string oneDecimal(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    std::string text = buffer;
    if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.erase(text.size() - 2);
    return text;
}

/// Seconds as florr's header prints them: always one decimal, "2.0s".
std::string reloadSeconds(double millis) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1fs", millis / 1000.0);
    return buffer;
}

/// florr's colour markup around `text`, for a TooltipLine with `markup` set.
std::string colourMarkup(const std::string& text, std::uint32_t rgb) {
    char open[24];
    std::snprintf(open, sizeof open, "<c val=\"#%06X\">", rgb & 0xFFFFFFu);
    return open + text + "</c>";
}

} // namespace

namespace ui {

/// Name / tier / description / HP / damage, in the shared petal tooltip's
/// order. HP and damage carry the account's skill tiers, since that is the
/// number the player will actually see in the field, and the reload carries
/// `reloadScale` -- the Reload talent and the bar's golden leaves -- for the
/// same reason.
std::vector<TooltipLine> equippedPetalTooltipLines(std::uint16_t originalIndex,
        const EquippedPetal& equipped, const SkillSet& skills, double reloadScale, int slot, double triangleBonus) {
    const auto effectiveStats = equippedPetalStats(content(), equipped);
    const auto baseStats = content().petalStats(equipped.configIndex, equipped.rarity);
    const double scale = baseStats.reloadMillis > 0.0
        ? reloadScale * effectiveStats.reloadMillis / baseStats.reloadMillis : reloadScale;
    const bool copiedSawblade = originalIndex != equipped.configIndex &&
        content().petal(equipped.configIndex).id == "sawblade";
    auto lines = petalTooltipLines(equipped.configIndex, equipped.rarity, skills, scale, slot,
                                   triangleBonus, samePetalTier(equipped.reloadRarity, equipped.rarity) && !copiedSawblade);
    if (originalIndex != equipped.configIndex && !lines.empty()) {
        lines.front().text = std::string(language() == Language::Zh ? "拟态 → " : "Mimic → ") + lines.front().text;
    }
    return lines;
}

std::vector<TooltipLine> petalTooltipLines(std::uint16_t petalIndex, Rarity rarity,
                                           const SkillSet& skills, double reloadScale, int slot, double triangleBonus,
                                           bool showHyperTechnique) {
    const PetalConfig& config = content().petal(petalIndex);
    const PetalStats stats = content().petalStats(petalIndex, rarity);
    const auto* technique = showHyperTechnique ? hyperTechniqueInfo(config.id, rarity) : nullptr;
    const double chargeScale = hyperAmount(technique, HyperTechnique::QuickCharge);
    const double health = std::round(stats.health > 1.0 ? stats.health * skills.healthScale(SkillId::PetalHealth) : stats.health);
    const double damageScale = skills.effectScale(SkillId::Damage);
    const double baseDamage = config.lightningDamage && stats.lightningDamagePerHit > 0
        ? stats.lightningDamagePerHit : stats.damage;
    const double damage = std::round((baseDamage + triangleBonus) * damageScale);

    std::vector<TooltipLine> lines;
    // florr's header: on the name's right, what bringing the petal out costs
    // in mana and how long its reload is, then the wait between spawning and
    // acting -- a rose's heal, an orb's mana -- when it has one ("3.5s +
    // 1.5s"). A worn petal has no reload and florr prints nothing for it.
    const std::string enName = titleCase(config.name);
    TooltipLine title{petalNameTr(config.id, enName.c_str()), 20.0};
    if (!config.noPhysics) {
        if (stats.requiredMana > 0.0) {
            // Paid on spawn, not on use -- see PetalConfig::requiredMana.
            title.badges.push_back({oneDecimal(stats.requiredMana), kStatMana, TooltipIcon::Mana});
            title.badges.push_back({" +"});
        }
        std::string reload = reloadSeconds(stats.reloadMillis * reloadScale);
        const double charge = stats.mana > 0.0                         ? stats.manaChargeMillis
                              : (stats.heal > 0.0 || stats.shield > 0.0) ? stats.healChargeMillis
                                                                         : 0.0;
        if (charge > 0.0) reload += " + " + reloadSeconds(charge * chargeScale);
        title.badges.push_back({reload, kPaper, TooltipIcon::Reload});
    }
    lines.push_back(title);
    lines.push_back({rarityTr(rarity), 14.0, rarityColor(rarity)});
    if (!config.description.empty()) {
        // florr's descriptions, from its English table, colour markup and
        // all. "{rarity}" is the card's own tier in its colour, as florr's
        // {0:rarity} is: a champion's crown hands out a Basic of whatever tier
        // the crown is.
        std::string description = petalDescTr(config.id, config.description.c_str());
        if (config.id == "triangle" && rarity == Rarity::Unique) {
            description = language() == Language::Zh
                ? "主栏中每多装备一枚永恒三角，所有永恒三角都会获得额外伤害。"
                : "Each additional Eternal Triangle on the active bar adds damage to every Eternal Triangle.";
        }
        static const std::string kRaritySlot = "{rarity}";
        for (std::size_t at = description.find(kRaritySlot); at != std::string::npos;
             at = description.find(kRaritySlot, at)) {
            const std::string tier = colourMarkup(rarityTr(rarity), rarityColor(rarity));
            description.replace(at, kRaritySlot.size(), tier);
            at += tier.size();
        }
        TooltipLine body{description, 12.0};
        body.gapBefore = 10.0;
        body.maxWidth = kTooltipWrapWidth;
        body.markup = true;
        lines.push_back(body);
    }

    if (computerWeapon(config.id)) {
        const bool zh = language() == Language::Zh;
        const auto addSetLine = [&](const char* text) {
            TooltipLine line{text, 12.0, kPaper};
            line.maxWidth = kTooltipWrapWidth;
            lines.push_back(line);
        };
        addSetLine(zh ? "电脑套装：主装备栏中，同阶或更高阶的组件按类别计数。"
                      : "Computer set: count distinct part types of this tier or higher on the active bar.");
        addSetLine(zh ? "六类组件：程序、内存、芯片、电容器、光标、几何体。"
                      : "Six part types: software, RAM, chip, capacitor, cursor, and geometry.");
        addSetLine(zh ? "3 类：基础伤害 +12%；4 类：额外增加 15% 耐久（原耐久须大于 1）；5 类：再缩短 10% 重装。"
                      : "3 types: +12% base damage. 4: also +15% durability if above 1. 5: also 10% shorter reload.");
        if (!computerPart(config.id)) {
            addSetLine(zh ? "这枚花瓣享受套装加成，自身不计作组件。"
                          : "This petal receives set bonuses and contributes no part type.");
        }
    }

    if (technique) {
        const bool zh = language() == Language::Zh;
        TooltipLine special{std::string(zh ? "超凡机制 · " : "Hyper technique · ") +
                            (zh ? technique->chinese : technique->english), 12.0, kPaper};
        special.gapBefore = 6.0;
        special.maxWidth = kTooltipWrapWidth;
        lines.push_back(special);
        TooltipLine hint{std::string(zh ? "神秘提示 · " : "Mysterious hint · ") +
                         hyperMysteryHint(technique->technique, zh), 12.0, kPaper};
        hint.alpha = 0.78;
        hint.maxWidth = kTooltipWrapWidth;
        lines.push_back(hint);
    }

    // Dimmed to gardn's 0xffffff90 rather than a pre-mixed grey, and spaced
    // off the description when there is one, off the tier row when there is not.
    TooltipLine hp{std::string(tipHp()) + abbreviate(health), 12.0, kPaper,
                   config.description.empty() ? 10.0 : 4.0};
    hp.alpha = 0.56;
    hp.labelColor = kStatHealth;
    hp.altText = std::string(tipHp()) + exactNumber(health);
    if (!stats.breakable) { hp.text = std::string(tipHp()) + (language() == Language::Zh ? "不可破坏" : "Unbreakable"); hp.altText = hp.text; }
    lines.push_back(hp);

    const std::string damageLabel = config.id == "battery" || config.id == "lightning" ||
        config.id == "mecha_missile"
        ? (language() == Language::Zh ? "接触伤害：" : "Contact damage: ") : tipDamage();
    TooltipLine hit{damageLabel + abbreviate(damage), 12.0};
    hit.alpha = 0.56;
    hit.labelColor = kStatDamage;
    hit.altText = damageLabel + exactNumber(damage);
    // A tomato's hit grows while it is out, so the card gives the range it
    // grows through, as florr's does.
    if (petalGrowsOverTime(config)) {
        const double grown = std::round(damage * kTomatoMaxGrowth);
        hit.text = damageLabel + abbreviate(damage) + " ~ " + abbreviate(grown);
        hit.altText = damageLabel + exactNumber(damage) + " ~ " + exactNumber(grown);
    }
    // Blueberries land every hit as lightning: florr's
    // Petal/Attribute/Damage/Lightning, "Damage: X (lightning)" with the
    // note in lightning cyan.
    if (config.lightningDamage) {
        const std::string label = colourMarkup(tipDamage(), kStatDamage);
        const std::string note = " " + colourMarkup(tipLightning(), kStatLightning);
        hit.text = label + " " + abbreviate(damage) + note;
        hit.altText = label + " " + exactNumber(damage) + note;
        hit.markup = true;
    }
    lines.push_back(hit);
    if (config.projectile.present) {
        const bool zh = language() == Language::Zh;
        lines.push_back({zh ? "发射物伤害加成：宠物伤害天赋。" :
            "Projectile damage scales with the Pet Damage talent.", 12.0});
        lines.push_back({zh ? "耐久天赋提高环上花瓣耐久；已发射物使用该阶基础耐久。" :
            "Health talents affect the ring petal. Shots use the tier's base health.", 12.0});
    } else if (config.petMobIndex == kInvalidIndex && stats.damage > 0.0) {
        lines.push_back({language() == Language::Zh ? "接触伤害加成：花瓣伤害天赋。" :
            "Contact damage scales with the Petal Damage talent.", 12.0});
    }

    // Burst / passive heal: field value includes kPlayerHealMultiplier (and the
    // Healing talent for the amount the flower actually receives).
    const double healTalent = skills.effectScale(SkillId::Medic);
    if (stats.heal > 0.0) {
        const double heal = std::round(stats.heal * healTalent * kPlayerHealMultiplier);
        TooltipLine line{std::string(tipHeal()) + abbreviate(heal), 12.0};
        line.alpha = 0.56;
        line.labelColor = kStatHeal;
        line.altText = std::string(tipHeal()) + exactNumber(heal);
        lines.push_back(line);
    }
    if (stats.passiveHealPerSecond > 0.0) {
        const double heal =
            std::round(stats.passiveHealPerSecond * healTalent * kPlayerHealMultiplier * 100.0) /
            100.0;
        TooltipLine line{std::string(tipHealPerSec()) + abbreviate(heal), 12.0};
        line.alpha = 0.56;
        line.labelColor = kStatHeal;
        line.altText = std::string(tipHealPerSec()) + oneDecimal(heal);
        lines.push_back(line);
    }
    if (healTalent * kPlayerHealMultiplier != 1.0 && (stats.heal > 0 || stats.passiveHealPerSecond > 0)) {
        const bool zh = language() == Language::Zh;
        const double base = stats.heal > 0 ? stats.heal : stats.passiveHealPerSecond;
        lines.push_back({std::string(zh ? "基础治疗：" : "Base healing: ") + exactNumber(base) +
            (stats.heal > 0 ? "" : (zh ? "/秒" : "/s")),12.0});
        lines.push_back({std::string(zh ? "本服治疗倍率：x" : "Server healing multiplier: x") + oneDecimal(kPlayerHealMultiplier),12.0});
        if (healTalent != 1) lines.push_back({std::string(zh ? "治疗天赋倍率：x" : "Healing talent: x") + oneDecimal(healTalent),12.0});
    }
    if (stats.shield > 0.0) {
        const double shield = std::round(stats.shield);
        TooltipLine line{std::string(tipShield()) + abbreviate(shield), 12.0};
        line.alpha = 0.56;
        line.labelColor = kStatGeneric;
        line.altText = std::string(tipShield()) + exactNumber(shield);
        lines.push_back(line);
    }
    if (stats.poisonPerSecond > 0.0) {
        const double pps = std::round(stats.poisonPerSecond);
        const double secs = stats.poisonDurationMillis / 1000.0;
        const char* sep = language() == Language::Zh ? "/秒 · " : "/s · ";
        TooltipLine line{std::string(tipPoison()) + abbreviate(pps) + sep + exactNumber(secs) + "s",
                         12.0};
        line.alpha = 0.56;
        line.labelColor = kStatPoison;
        line.altText =
            std::string(tipPoison()) + exactNumber(pps) + sep + exactNumber(secs) + "s";
        lines.push_back(line);
    }

    // Report the same effective numbers the server uses, including passive
    // effects that do not belong to the petal's contact damage or HP.
    const bool chinese = language() == Language::Zh;
    const auto special = [&](const char* zhLabel, const char* enLabel, double value,
                             const std::string& suffix = "") {
        TooltipLine line{std::string(chinese ? zhLabel : enLabel) + exactNumber(value) + suffix, 12.0};
        line.alpha = 0.72;line.labelColor = kStatGeneric;lines.push_back(line);
    };
    if (stats.flowerHealthBonus > 0) special("增加花朵生命：+", "Flower health: +", stats.flowerHealthBonus);
    if (config.id == "monstera") {
        special("范围治疗（每秒）：", "Area healing per second: ",
                monsteraHealPerSecond(rarity) * healTalent * kPlayerHealMultiplier);
        special("治疗半径：", "Healing radius: ", monsteraHealRadius(rarity));
    }
    if(config.burrowDurationMillis>0){
        special("潜地准备：", "Burrow charge: ",config.burrowChargeMillis/1000.0,"s");
        special("潜地持续：", "Burrow duration: ",config.burrowDurationMillis/1000.0,"s");
        lines.push_back({chinese?"准备后按防御潜地；仍会被Worm和沙漠蜈蚣命中，不可穿墙。":"Defend after charging to burrow. Worms and Desert Centipedes can still hit you; walls remain solid.",12.0});
    }
    if (stats.flowerRadiusBonus > 0) special("增加花朵半径：+", "Flower radius: +", stats.flowerRadiusBonus);
    if (!petalSharesFlowerDamage(config) && stats.modifiers.maxHealth != 1.0)
        special("最大生命：", "Max health: ", (stats.modifiers.maxHealth - 1.0) * 100.0, "%");
    if (stats.modifiers.speed != 1.0)
        special("移速变化：", "Movement speed: ", (stats.modifiers.speed - 1.0) * 100.0, "%");
    if (stats.modifiers.playerRadius != 1.0)
        special("花朵半径变化：", "Flower radius: ", (stats.modifiers.playerRadius - 1.0) * 100.0, "%");
    if (stats.modifiers.rotationSpeed != 1.0)
        special("旋转速度变化：", "Rotation speed: ", (stats.modifiers.rotationSpeed - 1.0) * 100.0, "%");
    if (stats.modifiers.attackRange > 0) special("攻击范围：+", "Attack range: +", stats.modifiers.attackRange);
    if (stats.modifiers.magnetism > 0) special("拾取范围：+", "Pickup range: +", stats.modifiers.magnetism);
    if (config.id == "pharaoh_s_crown") {
        const auto summonRarity = pharaohCrownSummonRarity(rarity);
        lines.push_back({std::string(chinese ? "召唤物稀有度：" : "Summon rarity: ") +
                         rarityTr(summonRarity), 12.0, rarityColor(summonRarity)});
        const auto mummy = content().mobIndex("mummy_beetle");
        if (mummy != kInvalidIndex) {
            const auto& mob = content().mob(mummy);
            special("召唤物生命：", "Summon health: ", mob.health * petalStatScale(summonRarity) *
                mob.petHealthScale * kSummonedMobHealthScale * skills.healthScale(SkillId::Summoner));
        }
    }
    if (config.petMobIndex != kInvalidIndex) {
        const auto& summon=content().mob(config.petMobIndex);
        const bool apex=ladderRarity(rarity)==Rarity::Apex;
        const auto summonRarity = petalGameplayRarity(rarity);
        const double summonScale=petalStatScale(summonRarity)*strengthBoost(rarity)*eggSummonCombatScale(config)*hyperSummonScale(config,rarity)*hyperSummonPromotionScale(config,rarity);
        TooltipLine pet{std::string(chinese ? "召唤：" : "Summons: ") +
            mobNameTr(content().mob(config.petMobIndex).id, content().mob(config.petMobIndex).name.c_str()) +
            " x" + std::to_string((config.id=="egg"||config.id=="beetle_egg"
                ? petalCopies(config.petCount,&skills,slot,petalTier(rarity),config.stackable)
                : config.petCount)*(apex?3:1)),12.0};lines.push_back(pet);
        special("召唤物生命：", "Summon health: ",summon.health*summonScale*summon.petHealthScale*kSummonedMobHealthScale*skills.healthScale(SkillId::Summoner)*eggSummonHealthScale(config));
        lines.push_back({std::string(chinese ? "召唤物稀有度：" : "Summon rarity: ") + rarityTr(summonRarity), 12.0});
        special("召唤物伤害：", "Summon damage: ",
                summon.damage * summonScale * summon.petDamageScale * summonDamageScale(config) *
                skills.effectScale(SkillId::PetDamage));
        if(apex && petalIsEgg(config))lines.push_back({chinese?"H护甲：己方召唤物受到不高于蛋稀有度的野怪体伤时，单次最多损失35%最大生命；不抵挡玩家、毒素或技能。":"H escort armour: wild body hits up to the egg tier deal at most 35% of summon max health per hit; no protection from players, poison or abilities.",12.0});
        if(config.id=="egg"||config.id=="beetle_egg")lines.push_back({chinese?"蛋壳不造成接触伤害；蛋壳破碎不会收回已孵化召唤物。":"The shell deals no contact damage; breaking it does not recall hatched pets.",12.0});
        if (config.petLifetimeMillis > 0) special("召唤持续：", "Summon lifetime: ", config.petLifetimeMillis / 1000.0, "s");
        if (config.id == "egg" || config.id == "beetle_egg") special("孵化等待：", "Hatch delay: ", 1.0, "s");
        if (config.petMobId != "sandstorm") lines.push_back({chinese ? "自动索敌优先主人600范围内的高阶怪，不超900；离主人700回跟，卡住会安全接回原召唤物。光标指挥优先。" : "Auto-targets high-tier mobs within 600 of the owner, capped at 900. Follows beyond 700; stuck summons recover. Cursor commands take priority.",12.0});
        if(config.id=="egg"||config.id=="beetle_egg")lines.push_back({chinese?"孵化准备的1秒内蛋壳受保护，不会被战斗打断。":"The shell is protected during its one-second incubation.",12.0});
    }
    if (config.projectile.summonMobIndex != kInvalidIndex) {
        const auto& summon = content().mob(config.projectile.summonMobIndex);
        const double scale = petalStatScale(rarity) * strengthBoost(rarity) * hyperSummonPromotionScale(config, rarity);
        lines.push_back({std::string(chinese ? "召唤物稀有度：" : "Summon rarity: ") +
                         rarityTr(petalGameplayRarity(rarity)), 12.0});
        special("每人召唤上限：", "Summon cap per player: ", config.projectile.summonMax);
        special("召唤物生命：", "Summon health: ", summon.health * scale * summon.petHealthScale * kSummonedMobHealthScale * skills.healthScale(SkillId::Summoner));
        special("召唤物伤害：", "Summon damage: ", summon.damage * scale * summon.petDamageScale * skills.effectScale(SkillId::PetDamage));
    }
    if(computerWeapon(config.id)) {
        lines.push_back({chinese?"电脑联动（只计算主槽、同阶或更高阶配件）：程序/内存/芯片/电容/光标/几何体为6种部件，重复不计。":"Computer synergy: active parts at this tier or higher; software/memory/chip/capacitor/cursor/geometry are six distinct roles.",12.0});
        lines.push_back({chinese?"3种：电脑花瓣伤害+12%；4种：耐久+15%；5种：装填时间−10%。低阶配件不会增幅高阶花瓣，拟态不补部件。":"3 roles: computer damage +12%; 4: health +15%; 5: reload time −10%. Lower-tier parts cannot boost higher tiers; Mimic does not count as hardware.",12.0});
    }
    if (config.id == "domino") {
        lines.push_back({chinese ? "伤害栏为平均值；每次重装独立掷两端0至6点，伤害随点数乘积变化，与幸运无关。" : "Damage shows the mean. Each reload rolls two independent ends from 0 to 6; their product determines damage, unaffected by Luck.",12.0});
        if (dominoHasReroll(rarity)) lines.push_back({chinese ? "H稳牌：每个零点端补掷一次，降低空牌概率；期望伤害仍为同种E的1.5倍。" : "H steady hand: reroll each zero end once, reducing blanks. Expected damage remains 1.5x the same Eternal petal.",12.0});
    }
    if (config.id == "dark_mark") {
        special("印记蓄力：", "Mark delay: ", darkMarkDelayMillis(rarity) / 1000.0, "s");
        lines.push_back({chinese ? "按攻击将印记放在倒下的玩家身上，蓄力后送往地狱；被救起或印记被打破则取消。" : "Attack over a fallen flower to place a mark. After charging, sends them to Hel; revival or breaking the mark cancels it.",12.0});
        lines.push_back({chinese ? "双方需20级；不作用于更高阶装备、分身、副本或已在地狱的玩家。成功后目标5分钟免疫Mark。" : "Both flowers must be level 20+. Cannot target higher-tier equipment, split flowers, dungeon runs or flowers in Hel. Target gains 5 minutes of Mark immunity.",12.0});
        if (ladderRarity(rarity) == Rarity::Apex) {
            special("地狱滞留：", "Hel hold: ", kHyperMarkHoldMillis / 1000.0, "s");
            lines.push_back({chinese ? "期间可正常移动战斗；死亡或返回菜单解除。到时可用 /mark-return 返回原处，不能叠加滞留。" : "Movement and combat remain available. Death or leaving ends the hold. Use /mark-return when it expires; holds cannot stack.",12.0});
        } else lines.push_back({chinese ? "无强制滞留，可立即用 /mark-return 返回原处。" : "No forced hold; /mark-return is immediately available.",12.0});
    }
    if (config.id == "corruption") {
        if (ladderRarity(rarity)==Rarity::Apex) {
            special("腐化光环半径：", "Corruption aura radius: ", kHyperCorruptionRadius);
            lines.push_back({chinese ? "装备时影响同地图范围内20级及以上玩家；离开即解除，不连锁传播。" : "While worn, affects nearby level 20+ flowers in the same realm; ends on leaving, never chains.",12.0});
            lines.push_back({chinese ? "使用者和光环影响者均可使用绷带。" : "Wearer and aura recipients can still use Bandage.",12.0});
        } else lines.push_back({chinese ? "腐化期间无法触发或维持绷带续命。" : "Corruption prevents and ends Bandage's Undead effect.",12.0});
    }
    if (config.id == "bandage") lines.push_back({chinese ? "普通或随机腐化会阻止并终止续命；H腐化及其光环不受此限制。" : "Ordinary or random corruption prevents and ends Undead; H Corruption and its aura are exempt.",12.0});
    if ((config.id == "sawblade" && ladderRarity(rarity) >= Rarity::Unique) ||
        (config.id == "rock" && isHyperPetal(rarity))) {
        lines.push_back({chinese ? "对野怪接触命中且花瓣未碎时，追加半伤害并承受半反伤。" :
                                   "After landing a contact hit on a wild mob, a surviving petal strikes again at half damage and takes half recoil.", 12.0});
    }
    if (stats.count > 1) special("每组花瓣数量：", "Petals per cluster: ", stats.count);
    if (stats.armorPerStack > 0) {
        special("每层护甲：", "Armor per stack: ", stats.armorPerStack);
        special("护甲叠层间隔：", "Armor stack interval: ", 2.0, "s");
        special("护甲层数上限：", "Armor stack limit: ", 10);
    }
    if (stats.noHealDurationMillis > 0) special("禁止治疗持续：", "Healing suppression: ", stats.noHealDurationMillis / 1000.0, "s");
    if (stats.knockback != 0) special("击退强度：", "Knockback: ", stats.knockback);
    if (stats.attractionForce > 0) special("花瓣吸引强度：", "Petal attraction strength: ", stats.attractionForce);
    if (stats.modifiers.damage != 1) special("伤害变化：", "Damage bonus: ", (stats.modifiers.damage - 1) * 100, "%");
    if (stats.modifiers.aggroRadius != 0) special("额外警觉范围：", "Extra aggro range: ", stats.modifiers.aggroRadius);
    if (stats.modifiers.petalAttractionRadius > 0) special("花瓣吸引范围：", "Petal attraction radius: ", stats.modifiers.petalAttractionRadius);
    if (!stats.breakable) lines.push_back({chinese ? "耐久：不可被攻击击碎" : "Durability: unbreakable",12.0});
    if (stats.webRadius > 0) special("蛛网半径：", "Web radius: ", stats.webRadius);
    if (stats.slowFactor < 1.0) {
        special("减速比例：", "Slow: ", (1.0 - stats.slowFactor) * 100.0, "%");
        if (stats.slowDurationMillis > 0) special("减速持续：", "Slow duration: ", stats.slowDurationMillis / 1000.0, "s");
    }
    if (stats.spongeDamageDurationMillis > 0)
        special("伤害分摊时间：", "Damage spread duration: ", stats.spongeDamageDurationMillis / 1000.0, "s");
    const double cameraZoom = config.id == "antennae" && !showHyperTechnique && isHyperPetal(rarity)
        ? petalCameraZoom(content(), petalIndex, Rarity::Unique) : stats.cameraZoom;
    if (cameraZoom != 1.0) {
        lines.push_back({std::string(chinese ? "视野缩放：" : "Camera zoom: ") +
            percentNumber(cameraZoom) + "%", 12.0});
        lines.push_back({std::string(chinese ? "可见范围倍率：" : "Vision range: ") +
            oneDecimal(1.0 / cameraZoom) + "x", 12.0});
    }
    if (config.id == "observer") lines.push_back({chinese ?
        "机械导弹发射时自动瞄准同地图内的敌方目标。" :
        "Mecha Missiles automatically aim at hostile targets in the same map on launch.",12.0});
    if (stats.modifiers.luck != 0.0)
        special(stats.modifiers.luck > 0 ? "幸运加成：+" : "幸运变化：",
                stats.modifiers.luck > 0 ? "Luck: +" : "Luck: ", stats.modifiers.luck);
    if (stats.modifiers.aggroRange != 1.0)
        special("怪物警觉范围变化：", "Mob detection range: ", (stats.modifiers.aggroRange - 1.0) * 100.0, "%");
    if (config.id == "bulb") {
        special("额外灯泡的引怪增量生效比例：", "Extra bulb aggro bonus retained: ",
                kAdditionalBulbAggroScale * 100.0, "%");
        special("叠加上限（基础警觉范围）：", "Stack cap (base aggro range): ",
                kAdditionalBulbAggroCap * 100.0, "%");
    }
    if (stats.modifiers.poisonArmor > 0) special("毒抗：", "Poison armor: ", stats.modifiers.poisonArmor);
    if (config.projectile.present) {
        if (config.id == "mecha_missile") {
            special("单发伤害：", "Damage per missile: ", baseDamage * skills.effectScale(SkillId::PetDamage));
            lines.push_back({chinese ? "单发伤害随召唤物伤害天赋提升。" :
                                       "Missile damage scales with the summon damage talent.", 12.0});
        }
        special("每轮弹数：", "Projectiles per volley: ", config.projectile.count);
        special("弹速：", "Projectile speed: ", config.projectile.speed);
        special("射程：", "Projectile range: ", config.projectile.distance);
        if (config.projectile.seekRange > 0) special("索敌范围：", "Seeking range: ", config.projectile.seekRange);
    }
    if (config.id == "magic_eye") {
        special("每次消耗当前魔力：", "Current mana spent per cast: ", 50.0, "%");
        special("每点耗蓝转化闪电伤害：", "Lightning damage per mana spent: ",
                magicEyeDamagePerMana(rarity));
        special("闪电索敌距离：", "Lightning targeting range: ", 240.0);
        special("最多连锁目标：", "Maximum lightning targets: ",
                std::clamp(std::min(rarityIndex(rarity), rarityIndex(Rarity::Unique)) - 3, 1, 5));
    }
    if (config.radiation.present) {
        if(config.id=="uranium")
            special("辐射自伤上限（每秒最大生命）：", "Self-radiation cap (% max HP/s): ", 4.0, "%");
        special("辐射半径：", "Radiation radius: ", config.radiation.radius);
        special("辐射间隔：", "Radiation interval: ", config.radiation.intervalMillis / 1000.0, "s");
        if (config.radiation.selfPoisonScale > 0)
            special("自身中毒倍率：", "Self-poison multiplier: ", config.radiation.selfPoisonScale);
    }
    if (stats.conditionalHealPerSecond > 0) {
        special("低于75%生命时治疗：", "Healing below 75% health: ",
            stats.conditionalHealPerSecond * healTalent * kPlayerHealMultiplier, chinese ? "/秒" : "/s");
        special("基础条件治疗：", "Base conditional healing: ", stats.conditionalHealPerSecond, chinese ? "/秒" : "/s");
    }
    if (stats.totemHealPerSecond > 0) {
        special("每次击杀增加回血：", "Heal per kill: ", stats.totemHealPerSecond * healTalent * kPlayerHealMultiplier,
                chinese ? "/秒，持续10秒，可叠加" : "/s for 10s, stacking");
    }
    if (stats.lightningDamagePerHit > 0) {
        if(config.id=="magic_peas")lines.push_back({chinese?"四颗蓝莓分别造成闪电伤害，不会连锁跳跃。":"Four blueberries deal lightning damage individually, with no chain jumps.",12.0});
        lines.push_back({chinese ? "闪电无视护甲和闪避。" : "Lightning bypasses armor and dodge.",12.0});
        special("基础闪电伤害：", "Base lightning damage: ", stats.lightningDamagePerHit);
        if (showHyperTechnique && stats.wildLightningScale > 1.0) {
            special("对野怪单次雷击：", "Lightning per hit on wild mobs: ",
                    stats.lightningDamagePerHit * stats.wildLightningScale * damageScale);
            special("野怪接触反伤减免：", "Wild contact recoil reduction: ",
                    (1.0 - stats.wildRecoilScale) * 100.0, "%");
        }
        special("连锁跳跃次数：", "Chain bounces: ", stats.lightningBounces);
        special("每段索敌范围：", "Range per jump: ", stats.lightningRange);
        if (stats.batteryCharges > 0) {
            special("每轮放电次数：", "Discharges per reload: ", stats.batteryCharges);
            lines.push_back({chinese ? "装备槽数字显示当前剩余放电次数。"
                                     : "The loadout counter shows remaining discharges.", 12.0});
            TooltipLine mechanism{chinese ? "花朵实际掉血时触发。两次放电至少间隔 0.5 秒，期间受到的有效伤害最多保留一次，等间隔结束再放电。3 次用完才开始重装；重装天赋只缩短回满时间。仅接触、护盾全吸收、自伤和无敌不触发。" :
                "Triggers on actual HP loss. Discharges are at least 0.5s apart, and one qualifying hit can wait until the interval ends. Reload talent only shortens the refill after all 3 charges are spent. Touching alone, fully shielded hits, self-damage and invulnerability do not trigger it.",12.0};
            mechanism.maxWidth=kTooltipWrapWidth; lines.push_back(mechanism);
        }
    }
    if(stats.lightningRepeatChance>0){
        special("闪电重复概率：", "Lightning repeat chance: ",stats.lightningRepeatChance*100.0,"%");
        special("最多追加电击：", "Maximum extra strikes: ",stats.maxLightningRepeats);
        lines.push_back({chinese?"只取最强红宝石；每次追加重新判定，不额外开启新连锁。":"Only the strongest Ruby applies. Extra strikes roll independently without starting another chain.",12.0});
    }

    // Relic. The whole petal is this one number.
    if (petalSharesFlowerDamage(config)) {
        TooltipLine bonus{std::string(tipFlowerHealth()) + "+" +
                              exactNumber((stats.modifiers.maxHealth - 1.0) * 100.0) + "%",
                          12.0};
        bonus.alpha = 0.56;
        bonus.labelColor = kStatHealth;
        lines.push_back(bonus);
    }

    // Rubber. Its price is printed beside what it buys.
    if (stats.flowerKnockback > 0.0) {
        TooltipLine shove{std::string(chinese ? "花朵受击击退：+" : "Flower Knockback: +") + exactNumber(stats.flowerKnockback * 100.0) + "%",
                          12.0};
        shove.alpha = 0.56;
        shove.labelColor = kStatGeneric;
        lines.push_back(shove);
    }

    // Amulet. florr's own line, and the whole of what the petal does.
    if (stats.overhealShield > 0.0) {
        TooltipLine convert{std::string(chinese ? "溢出治疗转护盾：+" : "Overheal Conversion: +") + exactNumber(stats.overhealShield * 100.0) +
                                "%",
                            12.0};
        convert.alpha = 0.56;
        convert.labelColor = kStatGeneric;
        lines.push_back(convert);
    }

    // Basil. Percentage of each heal (after kPlayerHealMultiplier), not a flat.
    if (stats.healBonus > 0.0) {
        TooltipLine bonus{std::string(tipHealBonus()) + percentNumber(stats.healBonus) + "%", 12.0};
        bonus.alpha = 0.56;
        bonus.labelColor = kStatHeal;
        lines.push_back(bonus);
    }

    // Golden leaf, florr's line: what it takes off the rest of the bar.
    if (stats.reloadCut > 0.0) {
        TooltipLine cut{std::string(chinese ? "全局冷却缩减：-" : "Reload: -") + percentNumber(stats.reloadCut) + "%", 12.0};
        cut.alpha = 0.56;
        cut.labelColor = kStatGeneric;
        lines.push_back(cut);
    }

    // Disc, florr's line, and the whole of what the petal does.
    if (stats.collisionResist > 0.0) {
        TooltipLine resist{std::string(chinese ? "碰撞伤害减免：+" : "Collision Damage Resistance: +") +
                               percentNumber(stats.collisionResist) + "%",
                           12.0};
        resist.alpha = 0.56;
        resist.labelColor = kStatGeneric;
        lines.push_back(resist);
    }

    // Bandage, florr's three lines: how long Undead lasts, what the body hits
    // for meanwhile, and how long the flower then cannot be revived.
    if (stats.undeadMillis > 0.0) {
        const auto seconds = [](double millis) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%g", std::round(millis / 10.0) / 100.0);
            return std::string(buffer) + "s";
        };
        TooltipLine undead{std::string(chinese ? "不死持续：" : "Undead: ") + seconds(stats.undeadMillis), 12.0};
        undead.alpha = 0.56;
        undead.labelColor = kStatUndead;
        lines.push_back(undead);
        TooltipLine body{std::string(tipBodyDamage()) + "-" +
                             percentNumber(stats.undeadBodyDamageCut) + "%",
                         12.0};
        body.alpha = 0.56;
        body.labelColor = kStatDamage;
        lines.push_back(body);
    }

    // Bur alone. Its description says the petal debuffs armour and cannot say
    // by how much, and how much is the entire question a player has about it:
    // whether this one out-strips the tier standing in front of them.
    if (stats.armorReduction > 0.0) {
        const char* debuff = language() == Language::Zh ? "护甲削弱: -" : "Armor Debuff: -";
        TooltipLine strip{std::string(debuff) + abbreviate(stats.armorReduction), 12.0};
        strip.alpha = 0.56;
        strip.labelColor = kStatArmor;
        strip.altText = std::string(debuff) + exactNumber(stats.armorReduction);
        lines.push_back(strip);
    }

    // Bone. Its armour is the whole of what the petal is for and it climbs the
    // mob armour ladder, so the figure a player is weighing belongs here.
    if (config.id == "bone") lines.push_back({chinese ? "骨头的护甲不会因受击而衰减。" : "Bone keeps its armor after taking a hit.",12.0});
    if (stats.petalArmor > 0.0) {
        TooltipLine armor{std::string(tipArmor()) + abbreviate(stats.petalArmor), 12.0};
        armor.alpha = 0.56;
        armor.labelColor = kStatArmor;
        armor.altText = std::string(tipArmor()) + exactNumber(stats.petalArmor);
        lines.push_back(armor);
    }

    // Claw. Petal damage like the line above, so it carries the same talent.
    if (stats.critDamage > 0.0) {
        const double crit = std::round(stats.critDamage * damageScale);
        TooltipLine bonus{std::string(tipCritDamage()) + "+" + abbreviate(crit), 12.0};
        bonus.alpha = 0.56;
        bonus.labelColor = kStatDamage;
        bonus.altText = std::string(tipCritDamage()) + "+" + exactNumber(crit);
        lines.push_back(bonus);
    }

    // Fang. landHeal multiplies the stolen amount by kPlayerHealMultiplier, so
    // the card shows the effective share of damage returned as HP.
    if (stats.lifesteal > 0.0) {
        special("基础吸血比例：", "Base lifesteal: ", stats.lifesteal * 100.0, "%");
        const double pct = stats.lifesteal * 100.0 * kPlayerHealMultiplier;
        TooltipLine steal{std::string(tipLifesteal()) + exactNumber(pct) + "%", 12.0};
        steal.alpha = 0.56;
        steal.labelColor = kStatHeal;
        lines.push_back(steal);
    }

    if (stats.petalEvasion > 0.0) {
        lines.push_back({std::string(chinese ? "花瓣自身闪避：" : "Petal evasion: ") + exactNumber(stats.petalEvasion * 100.0) + "%", 12.0});
        lines.push_back({chinese ? "仅保护花瓣自身；闪电不可闪避。" : "Protects this petal only; lightning bypasses evasion.", 12.0});
    }
    if (stats.petalPoisonImmune) lines.push_back({chinese ? "花瓣自身免疫中毒。" : "This petal is immune to poison.", 12.0});

    // Talisman. The chance grows a step every tier and nothing in play shows
    // it -- a dodge is a hit that simply did not happen -- so it is printed.
    if (stats.modifiers.evasion > 0.0) {
        TooltipLine dodge{std::string(chinese ? "闪避概率：" : "Evasion: ") + exactNumber(stats.modifiers.evasion * 100.0) + "%", 12.0};
        dodge.alpha = 0.56;
        dodge.labelColor = kStatGeneric;
        lines.push_back(dodge);
    }

    // Salt. What it repays depends on who bit, so the card states the one
    // fixed point -- an attacker of this card's own tier -- and the
    // description says how the rest of the ladder runs from there.
    if (stats.modifiers.damageReflection > 0.0) {
        TooltipLine reflect{std::string(chinese ? "反伤比例：" : "Reflection: ") +
                                exactNumber(stats.modifiers.damageReflection * 100.0) +
                                "% vs " + rarityTr(rarity),
                            12.0};
        reflect.alpha = 0.56;
        reflect.labelColor = kStatReflection;
        lines.push_back(reflect);
    }

    // The magic petals. Every one of these figures moves with rarity and none
    // of them can be read off the ring in play -- a player comparing two orbs
    // has nothing else to go on -- so they belong on the card. What a reload
    // costs is in the header, beside the reload; see the title above.
    //
    // The magic stick: paid per storm rather than per reload of the petal,
    // and the storm's span is what the payment buys. florr's
    // Petal/Attribute/SpawnManaCost, "Spawn Cost: X mana".
    if (stats.petManaCost > 0.0) {
        const std::string label = colourMarkup("Spawn Cost:", kStatGeneric) + " ";
        const std::string unit = " " + colourMarkup("mana", kStatMana);
        TooltipLine summon{label + abbreviate(stats.petManaCost) + unit, 12.0};
        summon.alpha = 0.56;
        summon.markup = true;
        summon.altText = label + exactNumber(stats.petManaCost) + unit;
        lines.push_back(summon);
    }
    if (config.petLifetimeMillis > 0.0) {
        TooltipLine span{std::string(chinese ? "召唤持续：" : "Summon Lifetime: ") + exactNumber(config.petLifetimeMillis / 1000.0) + "s",
                         12.0};
        span.alpha = 0.56;
        span.labelColor = kStatDuration;
        lines.push_back(span);
    }
    if (stats.maxMana > 0.0) {
        TooltipLine pool{std::string(chinese ? "魔法值上限：+" : "Max Mana: +") + abbreviate(stats.maxMana), 12.0};
        pool.alpha = 0.56;
        pool.labelColor = kStatMana;
        pool.altText = std::string(chinese ? "魔法值上限：+" : "Max Mana: +") + exactNumber(stats.maxMana);
        lines.push_back(pool);
    }
    if (config.id == "honey") {
        lines.push_back({chinese ? "投出的蜂蜜持续 12 秒，期间不会被怪物咬碎。" :
                                   "Thrown honey lasts 12 seconds and cannot be broken by mobs.", 12.0});
    }
    if (config.id == "magic_cotton") {
        TooltipLine absorption{
            std::string(chinese ? "每点法力吸收伤害：" : "Damage absorbed per mana: ") +
                oneDecimal(magicCottonDamagePerMana(rarity)), 12.0
        };
        absorption.alpha = 0.56;
        absorption.labelColor = kStatMana;
        absorption.altText = std::string(chinese ? "每点法力吸收伤害：" : "Damage absorbed per mana: ") +
            exactNumber(magicCottonDamagePerMana(rarity));
        lines.push_back(absorption);
    }
    const double manaTalent = skills.effectScale(SkillId::ManaRegen);
    if (stats.mana > 0.0) {
        TooltipLine burst{std::string(chinese ? "单次恢复魔法值：" : "Mana per Burst: ") + abbreviate(stats.mana * manaTalent), 12.0};
        burst.alpha = 0.56;
        burst.labelColor = kStatMana;
        burst.altText = std::string(chinese ? "单次恢复魔法值：" : "Mana per Burst: ") + exactNumber(stats.mana * manaTalent);
        lines.push_back(burst);
        const double cycle = stats.reloadMillis + stats.manaChargeMillis * chargeScale;
        if (cycle > 0.0) {
            TooltipLine average{std::string(chinese ? "完整周期平均回蓝/秒：" : "Full-cycle Mana/s: ") +
                                exactNumber(stats.mana * manaTalent * 1000.0 / cycle), 12.0};
            average.alpha = 0.56;
            average.labelColor = kStatMana;
            lines.push_back(average);
        }
    }
    if (stats.passiveManaPerSecond > 0.0) {
        TooltipLine regen{std::string(chinese ? "魔法恢复/秒：" : "Mana/s: ") + abbreviate(stats.passiveManaPerSecond * manaTalent), 12.0};
        regen.alpha = 0.56;
        regen.labelColor = kStatMana;
        regen.altText = std::string(chinese ? "魔法恢复/秒：" : "Mana/s: ") + exactNumber(stats.passiveManaPerSecond * manaTalent);
        lines.push_back(regen);
    }

    // Only the cutters have one, and theirs moves with rarity, so the number
    // has to be on the card -- the description alone cannot say how much.
    if (stats.bodyDamage > 0.0) {
        const double body = std::round(stats.bodyDamage);
        TooltipLine slam{std::string(tipBodyDamage()) + "+" + abbreviate(body), 12.0};
        slam.alpha = 0.56;
        slam.labelColor = kStatDamage;
        slam.altText = std::string(tipBodyDamage()) + "+" + exactNumber(body);
        lines.push_back(slam);
    }
    return lines;
}

} // namespace ui

double InventoryPanel::preferredWidth() {
    // Five cells, their gaps, and the two unequal margins. The panel is sized
    // to the grid rather than the grid fitted to the panel, so a change to the
    // cell size moves the card's edge instead of crowding its contents.
    return kColumns * kCellSize + (kColumns - 1) * kCellGap + kGridPadding + kGridGutter;
}

void InventoryPanel::reset() {
    // Only the hover and the field's focus. Scroll position, search text and
    // Stack mode all survive a close and reopen in the browser -- the panel is
    // a window onto the account, not a wizard that starts over.
    searchField_.blur();
    tooltipDelay.reset();
    touchInspect.reset();
    thumbDrag() = {};
}

bool InventoryPanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Profile& profile = ctx.net.profile();
    const Rect panel = ctx.bounds;
    const Vec2 mouse = ctx.mouse();
    // The grid's own span, and its centre. Not the panel's: the scroll thumb
    // takes a strip off the right, so the cells -- and the tier headings and
    // rules that introduce them -- sit slightly left of the card's middle.
    const double gridLeft = panel.x + kGridPadding;
    const double gridRight = panel.right() - kGridGutter;
    const double centre = (gridLeft + gridRight) * 0.5;

    panelCard(canvas, panel, kInventorySkin);
    panelTitle(canvas, panel, menuTitle(MenuId::Inventory),
               language() == Language::Zh ? "拖动花瓣以装备" : "Drag a petal to equip it");

    // --- header controls ---------------------------------------------------
    const Rect closeRect = closeButtonRect(panel);
    const Rect toggleBoxRect{panel.x + kControlsPad,
                             panel.y + kControlsY + (kControlsHeight - kToggleBoxSize) * 0.5,
                             kToggleBoxSize, kToggleBoxSize};
    const Rect toggleHit{panel.x + kControlsPad, panel.y + kControlsY, kToggleHitWidth,
                         kControlsHeight};
    const double searchX = panel.x + kControlsPad + kToggleHitWidth;
    const Rect searchRect{searchX, panel.y + kControlsY,
                          std::max(60.0, panel.right() - kControlsRightPad - searchX),
                          kControlsHeight};

    // Eased rather than snapped: the toggle is the only animated control on
    // the panel and a hard flip reads as a redraw glitch. The rate matches the
    // browser's per-frame quarter-of-the-gap at 60fps.
    const double target = stacked_ ? 1.0 : 0.0;
    easeToward(stackLerp_, target, kStackToggleEaseSeconds);
    if (std::fabs(stackLerp_ - target) < 0.01) stackLerp_ = target;

    toggleBox(canvas, toggleBoxRect, stackLerp_);
    TextStyle toggleLabel;
    toggleLabel.size = kToggleLabelSize;
    toggleLabel.roundJoin = true;
    text(canvas, tr(Msg::Stack), toggleBoxRect.right() + 5.0,
         toggleBoxRect.y + kToggleBoxSize * 0.5 + 1.0, toggleLabel);

    // No placeholder: the browser's field is a bare white box until it is typed
    // into, and a grey "Search" there reads as a value the filter is applying.
    inputField(canvas, searchRect, search_, "", searchField_.focused, ctx.timeSeconds,
               &searchField_);
    panelClose(canvas, closeRect, closeRect.contains(mouse));

    // --- content -----------------------------------------------------------
    const Rect view{panel.x + kMenuBorder, panel.y + kHeaderHeight, panel.w - kMenuBorder * 2,
                    std::max(0.0, panel.bottom() - (panel.y + kHeaderHeight) - kMenuBorder)};
    const double visibleH =
        std::max(0.0, panel.bottom() - (panel.y + kHeaderHeight) - kViewBottomPad);
    const double innerWidth = panel.w - kGridPadding - kGridGutter;
    // Leading and trailing whitespace is dropped before matching: the browser
    // trims the field's value, so a stray space must not filter the grid
    // empty. The shared trim takes spaces, tabs, CRs and LFs, which is every
    // blank this field can hold -- editText lets no other control byte into a
    // single-line field.
    const Grid grid = buildGrid(profile, innerWidth, stacked_, trimmed(lowerCase(search_)));

    scroll_.contentHeight = grid.contentHeight;
    scroll_.viewHeight = visibleH;
    const double maxScroll = scroll_.maxOffset();
    const Rect track = thumbTrack(panel, view.y, visibleH);

    // Thumb drag: grab anywhere in the right strip (gallery's rule). Applied
    // before wheel / touch so a press on the bar is not also a grid fling.
    ThumbDrag& drag = thumbDrag();
    if (!ctx.window.mouseDown(MouseButton::Left)) drag.active = false;
    if (ctx.pressed() && maxScroll > 0 && track.contains(mouse) &&
        !closeRect.contains(mouse) && !toggleHit.contains(mouse) && !searchRect.contains(mouse)) {
        drag.active = true;
        drag.startY = mouse.y;
        drag.startOffset = scroll_.offset;
    }
    if (drag.active) {
        scroll_.offset =
            drag.startOffset + (mouse.y - drag.startY) * (maxScroll / std::max(1.0, visibleH));
    } else if (panel.contains(mouse)) {
        // The browser's wheel listener is on the whole canvas, so the title and
        // the controls row scroll the grid too.
        scroll_.offset -= ctx.wheel() * kWheelStep;
    }
    // A finger drags the grid itself, not the header: the search field and
    // the toggle keep answering the moment they are touched. Skip while the
    // thumb is held -- that contact already owns the scroll.
    if (!drag.active) {
        scroll_.offset -= touchScroll(ctx.window, view, maxScroll > 0);
    }
    scroll_.offset = clamp(scroll_.offset, 0.0, maxScroll);

    const double originX = panel.x;
    const double originY = view.y - scroll_.offset;
    const double viewBottom = view.y + visibleH;

    // The browser rejects the header strip and then tests every laid-out cell,
    // including one the cull below will not paint, so the hit test is its own
    // pass rather than a side effect of drawing.
    //
    // Not while a finger is dragging the grid (gallery's rule): the cell under
    // it rides with the scroll, and a tip that followed would be noise. A TAP
    // still hits -- on a phone that is how petal stats are read at all.
    // Also quiet while the thumb is dragged: the strip is not a petal cell.
    int hovered = -1;
    Rect hoveredRect{};
    const bool touchPointer = ctx.window.pointerIsTouch();
    const bool touchScrolling = ctx.window.touchScrolling();
    if (panel.contains(mouse) && mouse.y >= view.y && !drag.active &&
        !(touchPointer && touchScrolling) && !track.contains(mouse)) {
        for (std::size_t i = 0; i < grid.cells.size(); ++i) {
            const Cell& cell = grid.cells[i];
            const Rect rect{originX + cell.rect.x, originY + cell.rect.y, cell.rect.w,
                            cell.rect.h};
            if (!rect.contains(mouse)) continue;
            hovered = static_cast<int>(i);
            hoveredRect = rect;
            break;
        }
    }

    canvas.save();
    canvas.beginPath();
    canvas.rect(static_cast<float>(view.x), static_cast<float>(view.y), static_cast<float>(view.w),
                static_cast<float>(view.h));
    canvas.clip();

    for (const Section& section : grid.sections) {
        const double labelY = originY + section.labelY;
        if (labelY < view.y - kLabelHeight || labelY >= viewBottom) continue;

        TextStyle label;
        label.size = kSectionLabelSize;
        label.align = Align::Centre;
        label.baseline = Baseline::Bottom;
        label.fill = rarityColor(section.rarity);
        const std::string caption = rarityTr(section.rarity);

        // The heading's outline is 60% black, not solid -- solid reads a whole
        // weight heavier. TextStyle carries no stroke alpha, so the outlined
        // text goes down once under a global alpha and the fill is then laid
        // over it opaque, which leaves exactly the browser's two layers.
        canvas.setGlobalAlpha(0.6f);
        text(canvas, caption, centre, labelY, label);
        canvas.setGlobalAlpha(1.0f);
        label.strokeWidth = 0.0;
        text(canvas, caption, centre, labelY, label);

        // Rules out to the grid's own edges, stopping clear of the text. They
        // are what makes a tier read as a section rather than a stray heading,
        // and running them to the GRID rather than to the panel is what keeps
        // the heading centred over the cells it introduces.
        const double half = measure(caption, label.size) * 0.5;
        const double ruleY = labelY - 6.0;
        setStroke(canvas, kInventorySkin.accent);
        canvas.setLineWidth(static_cast<float>(kRuleWidth));
        canvas.setLineCap("round");
        if (centre - half - kRuleGap > gridLeft) {
            canvas.beginPath();
            canvas.moveTo(static_cast<float>(gridLeft), static_cast<float>(ruleY));
            canvas.lineTo(static_cast<float>(centre - half - kRuleGap), static_cast<float>(ruleY));
            canvas.moveTo(static_cast<float>(centre + half + kRuleGap), static_cast<float>(ruleY));
            canvas.lineTo(static_cast<float>(gridRight), static_cast<float>(ruleY));
            canvas.stroke();
        }
        canvas.setLineCap("butt");
    }

    for (std::size_t i = 0; i < grid.cells.size(); ++i) {
        const Cell& cell = grid.cells[i];
        const Rect rect{originX + cell.rect.x, originY + cell.rect.y, cell.rect.w, cell.rect.h};
        if (rect.bottom() <= view.y || rect.y >= viewBottom) continue;

        ItemTile tile;
        tile.petalIndex = cell.petalIndex;
        tile.rarity = cell.rarity;
        // The badge is how many the ACCOUNT owns; the cluster the tile draws is
        // how many one equipped slot spawns. Two different counts, deliberately.
        // Admin omni bags are capped at kMaxStackCount — the x2.1B spam is noise.
        if (!ctx.net.isSkinAdmin() && cell.count > 1) {
            tile.badge = "x" + stackCountText(cell.count);
        }
        // No hover wash: the pointer turning into a hand is what says a cell
        // can be picked up (below), and the tile itself stays its own colour.
        tile.timeSeconds = ctx.timeSeconds;
        drawItemTile(canvas, ctx.sprites, rect, tile);
    }
    canvas.restore();

    scrollThumb(canvas, panel, view.y, visibleH, grid.contentHeight, scroll_.offset);

    // --- tooltip -----------------------------------------------------------
    // Anchored to the CELL rather than the cursor. Mouse: after the dwell, and
    // a press cancels it (drag starts next). Touch: gallery-style -- show
    // while the finger rests on a cell and is not pressing, dragging, or
    // scrolling. Press / drag / click-to-equip are unchanged; the tip is only
    // painted when those are idle.
    const bool pointerDown = ctx.window.mouseDown(MouseButton::Left) ||
                             ctx.window.mousePressed(MouseButton::Left) || ctx.drag.active();
    const auto& pan=ctx.window.touchPan();
    const bool holdTip = touchInspect.touch(ctx.window,ctx.timeSeconds,[&](Vec2 point){
        if(!view.contains(point)||track.contains(point))return -1;
        for(std::size_t i=0;i<grid.cells.size();++i){const auto& cell=grid.cells[i];
            if(Rect{originX+cell.rect.x,originY+cell.rect.y,cell.rect.w,cell.rect.h}.contains(point))return static_cast<int>(i);}
        return -1;
    },pan.active && view.contains({pan.originX,pan.originY}));
    if(holdTip && ctx.drag.source==DragState::Source::Inventory)ctx.drag.clear();
    const int tipIndex=holdTip?touchInspect.target:hovered;
    const bool showTooltip = holdTip || (hovered >= 0 &&
        (!touchPointer && tooltipDelay.update(hovered,ctx.timeSeconds,pointerDown)));
    if (showTooltip) {
        const Cell& cell = grid.cells[static_cast<std::size_t>(tipIndex)];
        // The reload the bar would actually run, as the slot wedge times it.
        const double reloadScale =
            profile.skills.reloadScale() *
            (1.0 - barReloadCut(content(), [&](int i) { return profile.equipped(content(), i); }, &profile.skills));
        const std::vector<TooltipLine> lines =
            petalTooltipLines(cell.petalIndex, cell.rarity, profile.skills, reloadScale);
        const bool alt = ctx.altMode;
        const Vec2 size = measureTooltip(lines, 0.0, 0.0, alt);
        const Rect tipRect{originX+cell.rect.x,originY+cell.rect.y,cell.rect.w,cell.rect.h};
        const Vec2 at = tooltipAnchor(tipRect, size, canvas.width(), canvas.height());
        paintTooltip(canvas, at.x, at.y, lines, 0.0, 0.0, alt);
    }

    // --- input -------------------------------------------------------------
    // The field's pointer handling runs unless Close or Stack is taking this
    // press: neither of those disturbs the caret in the browser, and blurring
    // on them would lose a half-typed filter to a stray click on the toggle.
    if (!(ctx.pressed() && (closeRect.contains(mouse) || toggleHit.contains(mouse)))) {
        trackTextMouse(ctx.window, searchField_, searchRect,
                       inputFieldRun(searchRect, search_, searchField_), search_,
                       ctx.timeSeconds);
    }
    if (searchField_.focused) {
        ctx.wantsText = true;
        TextEditOptions typing;
        typing.maxBytes = kSearchLimit;
        editText(ctx.window, search_, searchField_, ctx.timeSeconds, typing);
        if (ctx.window.keyPressed(Key::Enter) || ctx.window.keyPressed(Key::Escape)) {
            searchField_.blur();
        }
    }
    if (searchRect.contains(mouse)) ctx.window.setCursorShape(CursorShape::Text);
    // Held through a press as well: a drag starts on the press, and the hand
    // flipping back to an arrow the moment a petal is picked up reads as lost.
    if (hovered >= 0) ctx.window.setCursorShape(CursorShape::Hand);
    if (maxScroll > 0 && (track.contains(mouse) || drag.active)) {
        ctx.window.setCursorShape(CursorShape::Hand);
    }

    if (ctx.pressed()) {
        // Close and Stack act on PRESS, as the browser's mousedown handler
        // does. Both also swallow the press rather than letting it reach the
        // field or a cell, and neither disturbs the search focus -- the
        // browser's preventDefault leaves the input focused where it was.
        if (closeRect.contains(mouse)) return false;
        if (toggleHit.contains(mouse)) {
            // Scroll survives the flip: it is re-clamped against the new
            // content height on the next frame, which is all the browser does.
            stacked_ = !stacked_;
            return true;
        }
        // The field answered for its own press above.
        if (searchRect.contains(mouse)) return true;
        // Thumb strip owns the press: do not also pick up a petal under it.
        if (drag.active || track.contains(mouse)) return true;
        if (hovered >= 0 && !ctx.drag.active()) {
            const Cell& cell = grid.cells[static_cast<std::size_t>(hovered)];
            ctx.drag.source = DragState::Source::Inventory;
            ctx.drag.petalIndex = cell.petalIndex;
            ctx.drag.rarity = cell.rarity;
            ctx.drag.slot = -1;
        }
    }

    if (ctx.released() && touchInspect.consumedRelease) {
        if(ctx.drag.source==DragState::Source::Inventory)ctx.drag.clear();
        return true;
    }
    if (ctx.released() && hovered >= 0 && ctx.drag.source == DragState::Source::Inventory &&
        ctx.drag.petalIndex == grid.cells[static_cast<std::size_t>(hovered)].petalIndex &&
        ctx.drag.rarity == grid.cells[static_cast<std::size_t>(hovered)].rarity) {
        // Released on the cell it was picked up from: a click, not a drag.
        // Equip it into the first free slot, which is what a player wants nine
        // times out of ten and saves the trip to the bar.
        for (std::size_t slot = 0; slot < kLoadoutSlots; ++slot) {
            if (slot % kLoadoutActiveSlots >= static_cast<std::size_t>(profile.skills.loadoutSlots())) continue;
            if (slot < profile.loadout.size() && !profile.loadout[slot].empty()) continue;
            ctx.net.setLoadoutSlot(static_cast<int>(slot), ctx.drag.petalIndex, ctx.drag.rarity);
            break;
        }
        ctx.drag.clear();
    }
    return true;
}

} // namespace flix
