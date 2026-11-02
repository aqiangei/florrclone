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
    "永久保留，可重复锻造，不会被其他玩家收回。",
    "同种首次150、第二次300、之后600个超级。",
    "每种独立计价；产出永恒（原版唯一强度档）。",
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
std::uint32_t forgeInputOwned(const Profile& profile, std::uint16_t petalIndex) {
    return std::min<std::uint32_t>(profile.stackCount(petalIndex, kTitanInputRarity),
                                   static_cast<std::uint32_t>(kMaxStackCount));
}

} // namespace

Vec2 TitanPanel::size(const Profile& profile) {
    const std::size_t rows =
        std::max<std::size_t>(1, SlotTierGrid::rows(profile, kTitanInputRarity, kColumns));
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
    refusal_.clear();
    askedPetal_ = kNoPetal;
    askedAt_ = -1.0;
}

bool TitanPanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Profile& profile = ctx.net.profile();
    const Vec2 mouse = ctx.mouse();
    const double now = ctx.timeSeconds;
    const double dtMs = std::max(0.0, ctx.dt) * 1000.0;

    // A result the server sent while this face of the menu was not showing
    // still has to land, so it is read here rather than in the click.
    TitanForgeOutcome& outcome = ctx.net.titanForgeOutcome();
    if (outcome.pending) {
        outcome.pending = false;
        if (outcome.success && knownPetal(outcome.petalIndex)) {
            resultPetal_ = outcome.petalIndex;
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
    if (phase_ == Phase::Idle && knownPetal(stagedPetal_) &&
        (forgeInputOwned(profile, stagedPetal_) < static_cast<std::uint32_t>(titanForgeCost(profile.titanForgeCount(stagedPetal_))) ||
         !titanForgeEligible(content().petal(stagedPetal_).id))) {
        stagedPetal_ = kNoPetal;
    }

    const SlotCardLayout card = drawSlotCard(canvas, ctx.bounds, kTitanSkin, "泰坦锻造", "Forge", mouse);
    const Rect panel = card.panel;

    // --- the slot ----------------------------------------------------------
    // The five, staged or out with the titan, as one apex tile counting them;
    // the universal in their place once it is forged.
    const std::uint16_t shown = phase_ == Phase::Forging ? offeredPetal_ : stagedPetal_;
    ItemTile shownTile;
    shownTile.petalIndex = shown;
    shownTile.rarity = kTitanInputRarity;
    shownTile.badge = "x" + std::to_string(phase_ == Phase::Forging ? offeredCost_ : titanForgeCost(profile.titanForgeCount(shown)));
    shownTile.timeSeconds = now;
    // Lit under the cursor only while the slot is at rest.
    shownTile.hovered =
        motion_.spin() < 0.05 && phase_ == Phase::Idle && card.slotRect.contains(mouse);
    ItemTile forgedTile;
    forgedTile.petalIndex = resultPetal_;
    forgedTile.rarity = kTitanOutputRarity;
    forgedTile.timeSeconds = now;
    motion_.drawSlotScene(canvas, ctx.sprites, card, kTitanSkin, (now - phaseStarted_) * 1000.0,
                          dtMs, !forged && knownPetal(shown) ? &shownTile : nullptr, shown,
                          forged ? &forgedTile : nullptr,
                          static_cast<std::uint32_t>(resultPetal_) | 0x80000000u, kBurst);

    // --- forge button ------------------------------------------------------
    // The colour of the tier it forges toward once five are in the slot --
    // florr's forge wears its top tier's grey.
    const Rect button = card.button;
    const bool canForge = phase_ == Phase::Idle && knownPetal(stagedPetal_);
    drawSlotButton(canvas, button, "锻造",
                   canForge ? std::optional<std::uint32_t>(rarityColor(kTitanOutputRarity))
                            : std::nullopt,
                   button.contains(mouse),
                   button.contains(mouse) && ctx.window.mouseDown(MouseButton::Left));
    // A refusal stands in place of the card's lines, on the middle one, until
    // it expires.
    if (refusal_.empty() || now >= refusalUntil_) {
        refusal_.clear();
        for (std::size_t i = 0; i < kLines.size(); ++i) {
            SlotCardLayout line = card;
            line.lineY = panel.y + kFirstLineY + kLinePitch * static_cast<double>(i);
            drawSlotLine(canvas, line, kLines[i]);
        }
    } else {
        SlotCardLayout line = card;
        line.lineY = panel.y + kFirstLineY + kLinePitch * static_cast<double>(kLines.size() / 2);
        drawSlotLine(canvas, line, refusal_, kSlotCardRefusalInk);
    }

    if (knownPetal(stagedPetal_)) {
        SlotCardLayout line = card;
        line.lineY = (card.slotRect.bottom() + panel.y + kFirstLineY - kLinePitch * 0.5) * 0.5;
        drawSlotLine(canvas, line, "本次消耗：" + std::to_string(titanForgeCost(profile.titanForgeCount(stagedPetal_))) + " 个同种超级，成功率100%");
    }

    // --- the grid ----------------------------------------------------------
    // Same-kind Super stacks; each cell uses its own persistent forge price.
    const std::optional<SlotTierGrid::Pick> hovered = grid_.render(
        ctx, card, kTitanSkin, kTitanInputRarity, kColumns, kGridTop,
        [&](std::uint16_t petalIndex, std::uint32_t owned) {
            SlotCell cell;
            cell.count = owned;
            if (petalIndex == stagedPetal_) {
                const std::uint32_t staged = static_cast<std::uint32_t>(titanForgeCost(profile.titanForgeCount(stagedPetal_)));
                cell.count = owned > staged ? owned - staged : 0;
            }
            cell.greyed = cell.count < static_cast<std::uint32_t>(titanForgeCost(profile.titanForgeCount(petalIndex))) ||
                          !titanForgeEligible(content().petal(petalIndex).id);
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

    if (button.contains(mouse)) {
        if (canForge) {
            phase_ = Phase::Forging;
            phaseStarted_ = now;
            offeredPetal_ = stagedPetal_;
            offeredCost_ = titanForgeCost(profile.titanForgeCount(stagedPetal_));
            resultPending_ = false;
            refusal_.clear();
            ctx.net.requestTitanForge(stagedPetal_);
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
        ctx.net.forgetTitanHolder(askedPetal_);
        // A grey stack goes nowhere, quietly: its grey is the whole answer.
        // Any other one REPLACES the ring -- one forge takes five of one petal.
        if (!hovered->greyed) {
            stagedPetal_ = hovered->petalIndex;
            refusal_.clear();
        }
    }
    return true;
}

} // namespace flix
