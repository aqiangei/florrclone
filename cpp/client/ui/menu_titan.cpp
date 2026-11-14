// Permanent next-tier forge, priced for 30 independent loot rolls.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "client/ui/item_tile.h"
#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "client/ui/text.h"
#include "shared/game/config.h"
#include "shared/game/hyper_petal_techniques.h"
#include "shared/game/npc.h"

namespace flix {

using namespace flix::ui;

namespace {

/// The window: florr's 610, and as tall as its picker's rows under the
/// three lines, one row at the least. The picker is six cells wide and
/// starts 327.2 into the body (380.8 from the window's top); its rows are
/// 70 apart with 10 of padding top and bottom.
constexpr std::size_t kColumns = 6;
constexpr double kMaxHeight = 700.0;
constexpr double kGridTop = 380.8;
constexpr double kGridPitch = 70.0;
constexpr double kGridPadding = 10.0;

/// The three lines: 16 units on a 22.4 line, the first at the body's 250
/// (its middle 261.2 into the body), centred.
constexpr double kBodyTop = 53.6;
constexpr double kFirstLineY = kBodyTop + 250.0 + 11.2;
constexpr double kLinePitch = 22.4;
constexpr std::array<const char*, 3> kLines = {
    "永久永恒：15个超级碎片兑换1个永恒碎片。",
    "普通花瓣首次10、之后固定15个超级。",
    "限时锤请切换上方独立入口，每把1小时，离线计时。",
};

/// The forge's spin: toward 10 rad/s while it is out, easing both ways at
/// the same rate; the least it spins before its answer shows is ours.
constexpr double kSpinTarget = 10.0;
constexpr double kSpinRate = 0.00632163093946958;
constexpr double kForgeSpinMs = 1000.0;
/// florr's forge throws its grains mostly upward, at any angle.
constexpr SlotBurstShape kBurst{200.0, 0.0, kTau};

/// How often the titan is asked again about a petal it has not answered
/// for: the server drops a query that comes too soon after the last.
constexpr double kHolderRetrySeconds = 0.3;
/// How long the slot waits on the titan before it gives up on the answer.
/// The forge resolved server-side either way, and the profile will say how.
constexpr double kForgeTimeoutSeconds = 8.0;
constexpr double kRefusalSeconds = 3.0;

/// An apex stack as the staging counts it, held below what an int holds.
int forgeCost(const Profile& profile, std::uint16_t index, bool hyper, const NetClient& net) {
    if(hyper)return std::max(net.hyperInputCost(index),
        hyperForgeCost(0,index<content().petalCount()?content().petal(index).id:std::string{}));
    return titanForgeCost(profile.titanForgeCount(index),
        index < content().petalCount() ? content().petal(index).id : std::string());
}
std::uint32_t forgeInputOwned(const Profile& profile, std::uint16_t petalIndex, Rarity tier=kTitanInputRarity) {
    return std::min<std::uint32_t>(profile.stackCount(petalIndex, tier),
                                   static_cast<std::uint32_t>(kMaxStackCount));
}

} // namespace

Vec2 TitanPanel::size(const Profile& profile) {
    const std::size_t rows = std::max({std::size_t{1},
        SlotTierGrid::rows(profile, kTitanInputRarity, kColumns),
        SlotTierGrid::rows(profile, Rarity::Unique, kColumns),
        SlotTierGrid::rows(profile, Rarity::Apex, kColumns),
        SlotTierGrid::rows(profile, Rarity::Ascendant, kColumns)});
    return {slotCardColumnsWidth(kColumns),
            std::min(kMaxHeight,
                     kGridTop + kGridPadding * 2.0 + static_cast<double>(rows) * kGridPitch)};
}

void TitanPanel::reset() {
    grid_.reset();
    motion_.clear();
    stagedPetal_ = kNoPetal;
    phase_ = Phase::Idle;
    resultPending_ = false;
    confirmForge_ = false;
    confirmBatch_ = false;
    confirmAll_ = false;
    allForgeSummary_.clear();
    refusal_.clear();
    askedPetal_ = kNoPetal;
    askedAt_ = -1.0;
}

bool TitanPanel::render(MenuContext& ctx) {
    if(askedAt_<0&&askedPetal_==kNoPetal&&phase_==Phase::Idle)ctx.net.clearTitanQuotes();
    Canvas& canvas = ctx.canvas;
    const Profile& profile = ctx.net.profile();
    const Vec2 mouse = ctx.mouse();
    const double now = ctx.timeSeconds;
    const bool eternalInput = hyper_ || permanentHyper_ || apexHammer_;
    const bool contested = hyper_ || apexHammer_;
    const auto quoteIndex = [&](std::uint16_t index) { return apexHammer_ ? kNoPetal : index; };
    const Rarity inputTier = apexHammer_ ? Rarity::Ascendant : eternalInput ? Rarity::Unique : kTitanInputRarity;
    const Rarity outputTier = apexHammer_ ? Rarity::Ascendant : permanentHyper_ ? Rarity::PermanentHyper
                             : hyper_ ? Rarity::Apex : kTitanOutputRarity;
    const auto costOf = [&](std::uint16_t index) {
        if (apexHammer_) return kApexHammerForgeCost;
        return permanentHyper_ ? kPermanentHyperCost : forgeCost(profile, index, hyper_, ctx.net);
    };
    const auto canUse = [&](std::uint16_t index) {
        const auto& petal = content().petal(index);
        if (apexHammer_) return petal.id == "fragment";
        if (permanentHyper_)
            return permanentHyperForgeEligible(petal.id) && !petal.isAdminPetal &&
                   petalComesAt(petal, Rarity::Unique);
        if (hyper_) return hyperForgeEligible(petal.id);
        return titanForgeEligible(petal.id) && (eternalFragment_ || petal.id == "fragment");
    };
    const double dtMs = std::max(0.0, ctx.dt) * 1000.0;

    // A result the server sent while this face of the menu was not showing
    // still has to land, so it is read here rather than in the click.
    TitanForgeOutcome& outcome = ctx.net.titanForgeOutcome();
    if (outcome.pending) {
        outcome.pending = false;
        if (outcome.petalIndex == kNoPetal) {
            if (phase_ == Phase::Forging && offeredPetal_ == kNoPetal) phase_ = Phase::Idle;
            if (outcome.success) {
                allForgeSummary_ = outcome.reason;
            } else {
                refusal_ = outcome.reason;
                refusalUntil_ = now + kRefusalSeconds;
            }
        } else if (outcome.success && knownPetal(outcome.petalIndex)) {
            resultPetal_ = outcome.petalIndex;
            resultQuantity_ = outcome.quantity;
            // Mid-spin the result is only recorded: the spin decides when it
            // has run long enough. Anywhere else it lands now.
            if (phase_ == Phase::Forging) {
                resultPending_ = true;
            } else {
                phase_ = Phase::Result;
            }
            // It is this flower's now, and the titan remembers it so.
            ctx.net.forgetTitanHolder(outcome.petalIndex);
            askedAt_ = -1.0;
        } else if (!outcome.success) {
            // Refused: nothing left the inventory, so the five go back into
            // the slot, with the reason in place of the lines.
            if (phase_ == Phase::Forging) {
                stagedPetal_ = offeredPetal_;
                phase_ = Phase::Idle;
            }
            resultPending_ = false;
            refusal_ = outcome.reason.empty() ? "The titan refused." : outcome.reason;
            refusalUntil_ = now + kRefusalSeconds;
        }
    }

    if (phase_ == Phase::Forging) {
        const double elapsed = now - phaseStarted_;
        if (elapsed * 1000.0 >= kForgeSpinMs && resultPending_) {
            resultPending_ = false;
            phase_ = Phase::Result;
        } else if (elapsed >= kForgeTimeoutSeconds) {
            phase_ = Phase::Idle;
        }
    }
    const bool forged = phase_ == Phase::Result && knownPetal(resultPetal_);
    motion_.stepRoll(dtMs, phase_ == Phase::Forging, forged, kSpinTarget, kSpinRate, kSpinRate);

    // The slot cannot hold five the account no longer has, nor a petal it now
    // holds at universal -- but never while a forge is out: those five are the
    // server's, and a profile landing a frame ahead of the result would empty
    // the ring under its own animation.
    if (!abandon_ && phase_ == Phase::Idle && knownPetal(stagedPetal_) &&
        (forgeInputOwned(profile, stagedPetal_, inputTier) < static_cast<std::uint32_t>(costOf(stagedPetal_)) ||
         !canUse(stagedPetal_))) {
        stagedPetal_ = kNoPetal;
        confirmForge_ = false;
        confirmBatch_ = false;
    }

    const SlotCardLayout card = drawSlotCard(canvas, ctx.bounds, kTitanSkin,
                                            apexHammer_ ? "Apex 雷神之锤争夺" : abandon_ ? "解除超凡花瓣" : permanentHyper_ ? "永久超凡兑换" :
                                            hyper_ ? "超凡争夺锻造" : eternalFragment_ ? "永久永恒锻造" : "限时永恒锤（1小时）",
                                            "Forge", mouse);
    const Rect panel = card.panel;
    const char* modes[]={"永久永恒","限时锤（1h）","超凡争夺","解除超凡","永久超凡","Apex 雷神锤"};
    const double tabW = (panel.w - 36.0) / 6.0;
    const int activeTab = apexHammer_ ? 5 : permanentHyper_ ? 4 : abandon_ ? 3 : hyper_ ? 2 : eternalFragment_ ? 0 : 1;
    for(int i=0;i<6;++i){
        const Rect tab{panel.x+18+i*tabW,panel.y+62,tabW-6,32};
        const bool selected = i == activeTab;
        drawSlotButton(canvas,tab,modes[i],selected?std::optional<std::uint32_t>(rarityColor(outputTier)):std::nullopt,tab.contains(mouse),tab.contains(mouse)&&ctx.window.mouseDown(MouseButton::Left));
        if(ctx.pressed()&&tab.contains(mouse)&&phase_!=Phase::Forging){
            abandon_=i==3;hyper_=i==2;eternalFragment_=i!=1;permanentHyper_=i==4;apexHammer_=i==5;
            reset();return true;
        }
    }

    if (abandon_) {
        if (knownPetal(stagedPetal_) && profile.stackCount(stagedPetal_, Rarity::Apex) == 0) reset();
        if (knownPetal(stagedPetal_) && now - askedAt_ > 1) {
            ctx.net.requestTitanHolder(stagedPetal_);
            askedAt_ = now;
        }
        const auto* holder = knownPetal(stagedPetal_) ? ctx.net.titanHolder(stagedPetal_) : nullptr;
        const bool ownedHere = holder && !holder->empty() && *holder == profile.username;
        const Rect releaseButton{panel.x+panel.w*0.5+12,panel.y+225,panel.w*0.5-32,46};
        const Rect releaseSlot{panel.x+panel.w*0.25-45,panel.y+207,90,90};
        const bool hammer=knownPetal(stagedPetal_)&&content().petal(stagedPetal_).id=="mjolnir";
        SlotCardLayout line = card;
        line.lineY = panel.y + 139;
        drawSlotLine(canvas, line, "选择在这位泰坦处持有的超凡花瓣。");
        line.lineY += kLinePitch;
        drawSlotLine(canvas, line, hammer ? "解除会永久销毁花费的10个永恒碎片。" : "解除会永久销毁花费的5个永恒。");
        line.lineY += kLinePitch;
        drawSlotLine(canvas, line, "材料不予返还；所选超凡也会销毁，并释放名额。",kSlotCardRefusalInk);
        if (knownPetal(stagedPetal_)) {
            ItemTile tile;
            tile.petalIndex = stagedPetal_;
            tile.rarity = Rarity::Apex;
            tile.timeSeconds = now;
            drawItemTile(canvas, ctx.sprites, releaseSlot, tile);
        }
        drawSlotButton(canvas, releaseButton, confirmForge_ && ownedHere ? "确认永久销毁，不返还" : "解除所选超凡",
                       ownedHere ? std::optional<std::uint32_t>(0xA34137u) : std::nullopt,
                       releaseButton.contains(mouse), false);
        line.lineY = panel.y + kFirstLineY;
        drawSlotLine(canvas, line, !knownPetal(stagedPetal_) ? "请从下方选择" :
                     !holder ? "正在查询持有记录…" : ownedHere ? "此处持有，可解除" : "此处没有你的持有记录");
        const auto hovered = grid_.render(ctx, card, kTitanSkin, Rarity::Apex, kColumns, kGridTop,
            [](std::uint16_t, std::uint32_t count) {
                SlotCell cell; cell.count = count;
                if (count > 1) cell.badge = "x" + stackCountText(count);
                return cell;
            });
        if (!ctx.pressed()) return true;
        if (card.close.contains(mouse)) return false;
        if (hovered) {
            stagedPetal_ = askedPetal_ = hovered->petalIndex;
            ctx.net.forgetTitanHolder(stagedPetal_);
            askedAt_ = -10;
            confirmForge_ = false;
        } else if (ownedHere && releaseButton.contains(mouse)) {
            if (!confirmForge_) confirmForge_ = true;
            else {
                ctx.net.sendChat("/hyper-abandon here " + content().petal(stagedPetal_).id + " confirm");
                ctx.net.forgetTitanHolder(stagedPetal_);
                reset();
            }
        } else confirmForge_ = false;
        return true;
    }

    // --- the slot ----------------------------------------------------------
    // The five, staged or out with the titan, as one apex tile counting them;
    // the universal in their place once it is forged.
    const std::uint16_t shown = phase_ == Phase::Forging ? offeredPetal_ : stagedPetal_;
    ItemTile shownTile;
    shownTile.petalIndex = shown;
    shownTile.rarity = inputTier;
    shownTile.badge = "x" + std::to_string(phase_ == Phase::Forging ? offeredCost_ : costOf(shown));
    shownTile.timeSeconds = now;
    // Lit under the cursor only while the slot is at rest.
    shownTile.hovered =
        motion_.spin() < 0.05 && phase_ == Phase::Idle && card.slotRect.contains(mouse);
    ItemTile forgedTile;
    forgedTile.petalIndex = resultPetal_;
    forgedTile.rarity = outputTier;
    if (resultQuantity_ > 1) forgedTile.badge = "x" + stackCountText(resultQuantity_);
    forgedTile.timeSeconds = now;
    motion_.drawSlotScene(canvas, ctx.sprites, card, kTitanSkin, (now - phaseStarted_) * 1000.0,
                          dtMs, !forged && knownPetal(shown) ? &shownTile : nullptr, shown,
                          forged ? &forgedTile : nullptr,
                          static_cast<std::uint32_t>(resultPetal_) | 0x80000000u, kBurst);

    // --- forge button ------------------------------------------------------
    // The colour of the tier it forges toward once five are in the slot --
    // florr's forge wears its top tier's grey.
    const bool permanent = !eternalInput && eternalFragment_;
    Rect button = card.button;
    Rect batchButton{};
    if (permanent) {
        constexpr double gap = 10.0;
        constexpr double width = 110.0;
        const double centre = button.x + button.w * 0.5;
        button.x = centre - gap * 0.5 - width;
        button.w = width;
        batchButton = {centre + gap * 0.5, button.y, width, button.h};
    }
    const auto* holder=knownPetal(stagedPetal_)?ctx.net.titanHolder(quoteIndex(stagedPetal_)):nullptr;
    const bool canForge = phase_ == Phase::Idle && knownPetal(stagedPetal_) &&
        (!contested || (holder&&ctx.net.hyperHoldings()<kMaxHyperHoldings&&ctx.net.hyperProtectionLeft(quoteIndex(stagedPetal_))<=0 &&
                     *holder!=profile.username));
    const bool fragment=knownPetal(stagedPetal_)&&content().petal(stagedPetal_).id=="fragment";
    const char* action=fragment&&!eternalInput?(eternalFragment_?(confirmForge_ && !confirmBatch_?"确认：E碎片":"兑换E碎片"):(confirmForge_ && !confirmBatch_?"确认：限时锤":"制作限时锤")):(confirmForge_ && !confirmBatch_?"确认锻造":"锻造");
    drawSlotButton(canvas, button, action,
                   canForge ? std::optional<std::uint32_t>(rarityColor(outputTier))
                            : std::nullopt,
                   button.contains(mouse),
                   button.contains(mouse) && ctx.window.mouseDown(MouseButton::Left));
    TitanForgeBatch batch;
    if (permanent && canForge) {
        const auto owned = forgeInputOwned(profile, stagedPetal_);
        const auto held = std::min(profile.stackCount(stagedPetal_, outputTier),
                                   static_cast<std::uint32_t>(kMaxStackCount));
        batch = titanForgeBatch(static_cast<int>(owned), kMaxStackCount - static_cast<int>(held),
                                profile.titanForgeCount(stagedPetal_), content().petal(stagedPetal_).id);
    }
    const bool canBatch = batch.quantity > 1;
    constexpr double allWidth = 230.0;
    const Rect allButton{card.button.x + (card.button.w - allWidth) * 0.5,
                         button.bottom() + 8.0, allWidth, button.h};
    const bool canForgeAll = (permanent || permanentHyper_) && phase_ == Phase::Idle &&
        std::any_of(profile.inventory.begin(), profile.inventory.end(), [&](const auto& stack) {
            if (stack.rarity != inputTier || !knownPetal(stack.petalIndex) ||
                !canUse(stack.petalIndex)) return false;
            return stack.count >= static_cast<std::uint32_t>(costOf(stack.petalIndex)) &&
                profile.stackCount(stack.petalIndex, outputTier) <
                    static_cast<std::uint32_t>(kMaxStackCount);
        });
    if (permanent) {
        drawSlotButton(canvas, batchButton, confirmBatch_ && canBatch ? "确认批量" : "批量兑换",
                       canBatch ? std::optional<std::uint32_t>(rarityColor(outputTier)) : std::nullopt,
                       batchButton.contains(mouse),
                       batchButton.contains(mouse) && ctx.window.mouseDown(MouseButton::Left));
    }
    if (permanent || permanentHyper_) {
        const bool working = phase_ == Phase::Forging && offeredPetal_ == kNoPetal;
        const char* label = permanentHyper_
            ? (confirmAll_ && canForgeAll ? "确认兑换全部永久超凡" : "一键兑换全部永久超凡")
            : (confirmAll_ && canForgeAll ? "确认锻造全部永恒" : "一键锻造全部永恒");
        drawSlotButton(canvas, allButton, working ? "正在兑换…" : label,
                       canForgeAll ? std::optional<std::uint32_t>(rarityColor(outputTier)) : std::nullopt,
                       allButton.contains(mouse), allButton.contains(mouse) && ctx.window.mouseDown(MouseButton::Left));
    }
    // A refusal stands in place of the card's lines, on the middle one, until
    // it expires.
    if (refusal_.empty() || now >= refusalUntil_) {
        refusal_.clear();
        for (std::size_t i = 0; i < kLines.size(); ++i) {
            SlotCardLayout line = card;
            line.lineY = panel.y + kFirstLineY + kLinePitch * static_cast<double>(i);
            if (i == 0 && confirmAll_ && canForgeAll) {
                drawSlotLine(canvas, line, permanentHyper_ ? "确认后将消耗背包内可兑换的永恒材料。"
                                                           : "确认后将消耗背包内所有可锻造的超级材料。");
                continue;
            }
            if ((permanent || permanentHyper_) && i == 0 && !allForgeSummary_.empty()) {
                line.line2Y = line.lineY;
                drawSlotLine(canvas, line, allForgeSummary_, std::nullopt, true);
                continue;
            }
            if (apexHammer_) {
                constexpr std::array<const char*, 3> rules = {
                    "5 个未装备的 Apex 碎片 → 1 把 Apex 雷神之锤",
                    "用永久超凡碎片在普通合成或海洋神使处获得 Apex 碎片",
                    "首保15小时，每次争夺减1小时，最低1小时。被夺返还4个碎片。"
                };
                drawSlotLine(canvas, line, rules[i]);
                continue;
            }
            if (permanentHyper_) {
                constexpr std::array<const char*, 3> rules = {
                    "30 个未装备的同种永恒 → 1 个永久超凡",
                    "碎片也可兑换，雷神之锤和限时锤不参与",
                    "数值与技能等同超凡，永久归属"
                };
                drawSlotLine(canvas, line, rules[i]);
                continue;
            }
            if(!hyper_ && i==0 && knownPetal(stagedPetal_) && content().petal(stagedPetal_).id=="fragment"){drawSlotLine(canvas,line,eternalFragment_?"15个超级碎片 → 1个永久永恒碎片":"15个超级碎片 → 1把永恒限时锤（1小时）");continue;}
            if (hyper_ && i == 0 && knownPetal(stagedPetal_)) {
                const auto& id = content().petal(stagedPetal_).id;
                if (const auto* technique = hyperTechniqueInfo(id, outputTier)) {
                    line.line2Y = line.lineY;
                    drawSlotLine(canvas, line, language() == Language::Zh ? technique->chinese
                                                                          : technique->english,
                                 rarityColor(outputTier), true);
                    continue;
                }
            }
            drawSlotLine(canvas, line, hyper_?std::array<const char*,3>{"超凡锤需10个永恒碎片；限时锤不能参与争夺。","争夺超凡上限为5份，背包、装备与两处泰坦登记均计入。","首保15小时，每次争夺减1小时，最低1小时。"}[i]:!eternalFragment_?std::array<const char*,3>{"15个超级碎片 → 1把永恒限时锤（1小时）","限时锤不能用于超凡争夺，不返还碎片。","可持有多把，每把独立计时1小时，离线计时。"}[i]:kLines[i]);
        }
    } else {
        SlotCardLayout line = card;
        line.lineY = panel.y + kFirstLineY + kLinePitch * static_cast<double>(kLines.size() / 2);
        drawSlotLine(canvas, line, refusal_, kSlotCardRefusalInk);
    }

    if (knownPetal(stagedPetal_)) {
        SlotCardLayout line = card;
        line.lineY = (card.slotRect.bottom() + panel.y + kFirstLineY - kLinePitch * 0.5) * 0.5;
        if (confirmBatch_ && canBatch)
            drawSlotLine(canvas, line, std::to_string(batch.cost) + " 个超级 → " +
                         std::to_string(batch.quantity) + " 个永久永恒");
        else drawSlotLine(canvas, line, "本次消耗：" + std::to_string(costOf(stagedPetal_)) + (apexHammer_ ? " 个 Apex 碎片，成功率100%" : eternalInput ? " 个同种永恒，成功率100%" : " 个同种超级，成功率100%"));
    }

    if (contested && knownPetal(stagedPetal_)) {
        if (now - askedAt_ > 1) {
            ctx.net.requestTitanHolder(quoteIndex(stagedPetal_));
            askedAt_ = now;
        }
        SlotCardLayout line = card;
        line.lineY = panel.y + 113;
        const auto held = ctx.net.hyperHoldings();
        const auto wait = ctx.net.hyperProtectionLeft(quoteIndex(stagedPetal_));
        if (!holder) {
            drawSlotLine(canvas, line, "正在查询泰坦…请站在泰坦附近。");
        } else if (held >= kMaxHyperHoldings) {
            drawSlotLine(canvas, line, "争夺超凡已持有 " + std::to_string(held) + "/" +
                         std::to_string(kMaxHyperHoldings) + "，已达上限。", kSlotCardRefusalInk);
        } else if (*holder == profile.username) {
            drawSlotLine(canvas, line, apexHammer_ ? "你已经持有这位泰坦的 Apex 雷神之锤。" : "你已经持有这位泰坦的该超凡。", kSlotCardRefusalInk);
        } else {
            drawSlotLine(canvas, line, std::string("持有者：") + (holder->empty() ? "暂无" : *holder) +
                         "  保护剩余：" + std::to_string(static_cast<int>(std::ceil(wait / 60000))) +
                         "分钟  已持有 " + std::to_string(held) + "/" + std::to_string(kMaxHyperHoldings));
        }
    }

    // --- the grid ----------------------------------------------------------
    // Same-kind Super stacks; each cell uses its own persistent forge price.
    const std::optional<SlotTierGrid::Pick> hovered = grid_.render(
        ctx, card, kTitanSkin, inputTier, kColumns, kGridTop,
        [&](std::uint16_t petalIndex, std::uint32_t owned) {
            SlotCell cell;
            cell.count = owned;
            if (petalIndex == stagedPetal_) {
                const std::uint32_t staged = static_cast<std::uint32_t>(costOf(stagedPetal_));
                cell.count = owned > staged ? owned - staged : 0;
            }
            cell.greyed = cell.count < static_cast<std::uint32_t>(costOf(petalIndex)) ||
                          !canUse(petalIndex);
            // A lone petal carries no badge, as in the inventory.
            if (cell.count > 1) cell.badge = "x" + stackCountText(cell.count);
            return cell;
        });

    // Tiles on their way out go over everything but the words.
    motion_.drawVanishing(canvas, ctx.sprites, dtMs);

    // --- input -------------------------------------------------------------
    // On press, as the forge, the oracle and the trader answer.
    const bool rightPressed = panel.contains(mouse) && ctx.window.mousePressed(MouseButton::Right);
    if (phase_ == Phase::Idle && rightPressed) {
        stagedPetal_ = kNoPetal;
        askedPetal_ = kNoPetal;
        return true;
    }
    if (!ctx.pressed() && !rightPressed) return true;
    if (card.close.contains(mouse) && ctx.pressed()) return false;

    // The universal sits there until it is dismissed, and the press that
    // dismisses it does nothing else.
    if (phase_ == Phase::Result && panel.contains(mouse)) {
        phase_ = Phase::Idle;
        resultPetal_ = kNoPetal;
        return true;
    }

    if ((permanent || permanentHyper_) && allButton.contains(mouse) && ctx.pressed()) {
        if (canForgeAll) {
            if (confirmAll_) {
                phase_ = Phase::Forging;
                phaseStarted_ = now;
                offeredPetal_ = kNoPetal;
                resultPending_ = false;
                allForgeSummary_.clear();
                refusal_.clear();
                if (permanentHyper_) ctx.net.requestPermanentHyperForge(kNoPetal);
                else ctx.net.requestTitanForgeAll();
            }
            confirmAll_ = !confirmAll_;
            confirmForge_ = false;
            confirmBatch_ = false;
        }
        return true;
    }
    confirmAll_ = false;
    const bool batchPressed = permanent && batchButton.contains(mouse);
    if (button.contains(mouse) || batchPressed) {
        if (batchPressed ? canBatch : canForge) {
            const bool confirmed = confirmForge_ && confirmBatch_ == batchPressed;
            confirmForge_ = !confirmed;
            confirmBatch_ = !confirmed && batchPressed;
            if (!confirmed) return true;
            phase_ = Phase::Forging;
            phaseStarted_ = now;
            offeredPetal_ = stagedPetal_;
            offeredCost_ = batchPressed ? batch.cost : costOf(stagedPetal_);
            resultPending_ = false;
            refusal_.clear();
            if (apexHammer_) ctx.net.requestApexHammerForge(stagedPetal_);
            else if (permanentHyper_) ctx.net.requestPermanentHyperForge(stagedPetal_);
            else ctx.net.requestTitanForge(stagedPetal_, hyper_, eternalFragment_, batchPressed);
            // Handed over: the ring draws the offer from here, and a refusal
            // is what puts it back.
            stagedPetal_ = kNoPetal;
        }
        return true;
    }

    if (phase_ == Phase::Idle && card.slotRect.contains(mouse)) {
        stagedPetal_ = kNoPetal;
        askedPetal_ = kNoPetal;
        return true;
    }

    if (hovered && phase_ == Phase::Idle && ctx.pressed()) {
        // Whatever is picked, the titan is asked afresh who it forged it for:
        // somebody may have forged it since it was last asked.
        askedPetal_ = hovered->petalIndex;
        askedAt_ = -1.0;
        ctx.net.forgetTitanHolder(quoteIndex(askedPetal_));
        // Explain a disabled stack; colour alone cannot explain a recipe.
        // Any other one REPLACES the ring -- one forge takes five of one petal.
        if (!hovered->greyed) {
            confirmForge_ = false;
            confirmBatch_ = false;
            stagedPetal_ = hovered->petalIndex;
            refusal_.clear();
        } else {
            const auto& id=content().petal(hovered->petalIndex).id;
            refusal_=apexHammer_?"需要5个未装备的Apex碎片。":permanentHyper_?"需要 30 个未装备的同种永恒花瓣。雷神之锤和限时锤不参与兑换。":
                hyper_&&(id=="timed_mjolnir"||id=="mjolnir")?"超凡锤子需10个永恒碎片，限时锤不能参与争夺。":
                "材料不足或不可锻造；超凡需5个未装备的同种永恒。";
            refusalUntil_=now+kRefusalSeconds;
        }
    }
    return true;
}

} // namespace flix
