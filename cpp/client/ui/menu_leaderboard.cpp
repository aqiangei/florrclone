// Account rankings.
//
// Ranked over accounts rather than over who happens to be online, so the board
// is a record of progress and not a snapshot of the lobby. The server answers
// on request; the panel asks once when it opens and again only when told to.

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "client/ui/content_i18n.h"
#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "client/ui/text.h"

namespace flix {

using namespace flix::ui;

namespace {

/// The card is a fixed 500x500 pinned under the top icon row, and every metric
/// here is measured off it rather than off the window.
constexpr double kHeaderHeight = 90.0;   ///< chrome above the scrolling body
constexpr double kRowHeight = 40.0;
constexpr double kColumnHeader = 30.0;
constexpr double kScrollbarWidth = 10.0;
/// ui::scrollbar measures its viewport as the track plus this: the track stops
/// this much short of the bottom of the span the board scrolls through.
constexpr double kTrackBottomInset = 5.0;
/// A thumb drag maps the pointer over the card height less this, which is the
/// two insets the track sits between rather than the track's own length.
constexpr double kThumbTravelInset = 45.0;
/// A wheel notch carries no pixel count of its own here, but the browser panel
/// scrolls by the wheel event's raw delta -- ~100px a notch in Chrome -- so
/// the distance is spelled out to land on the same place.
constexpr double kWheelStep = 100.0;

/// Gold, silver, bronze. The only three rows that get a colour, because a
/// board where every row is coloured has no podium.
std::uint32_t rankColor(int rank) {
    switch (rank) {
        case 1: return 0xFFD700u;
        case 2: return 0xC0C0C0u;
        case 3: return 0xCD7F32u;
        default: return 0xFFFFFFu;
    }
}

/// The board's own number format: tenths of a K or an M, and nothing above.
/// `abbreviate` is nearly this but erases a trailing ".0" and has B and T
/// tiers, so the same total would read differently here than it did in the
/// browser.
std::string formatXp(double xp) {
    char buffer[32];
    if (xp >= 1e6) std::snprintf(buffer, sizeof buffer, "%.1fM", xp / 1e6);
    else if (xp >= 1e3) std::snprintf(buffer, sizeof buffer, "%.1fK", xp / 1e3);
    else std::snprintf(buffer, sizeof buffer, "%.0f", xp);
    return buffer;
}

double practiceRowHeight(const PracticeBoardRow& row) {
    return kRowHeight + 18.0 * ((row.loadout.size() + 1) / 2);
}

std::string practiceDate(double millis) {
    if (millis <= 0.0) return {};
    const auto seconds = static_cast<std::time_t>(millis / 1000.0);
    const auto* utc = std::gmtime(&seconds);
    char buffer[28]{};
    if (utc) std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M UTC", utc);
    return buffer;
}

struct BarDrag {
    bool active = false;
    double anchorY = 0;
    double anchorScroll = 0;
};

BarDrag& barDrag() {
    static BarDrag drag;
    return drag;
}

} // namespace

double LeaderboardPanel::preferredWidth() { return 500.0; }

void LeaderboardPanel::reset() {
    scroll_ = {};
    requested_ = false;
    hyper_ = false;
    playtime_ = false;
    damage_ = false;
    barDrag() = {};
}

bool LeaderboardPanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Rect panel = ctx.bounds;
    const Vec2 mouse = ctx.mouse();

    // Asked for on the way in rather than every frame: the server walks its
    // whole account table to answer, and the board does not move that fast.
    if (!requested_) {
        requested_ = true;
        ctx.net.requestLeaderboard(ctx.settings.showAdminsOnLeaderboard);
    }

    const std::vector<LeaderboardRow>& rows = playtime_ ? ctx.net.playtimeLeaderboard() : ctx.net.leaderboard();
    const auto& holdings = ctx.net.hyperBoard();
    const auto& practice = ctx.net.practiceBoard();
    const bool pending = damage_ ? practice.pending : ctx.net.leaderboardPending();
    const std::size_t rowCount = hyper_ ? holdings.size() : rows.size();
    const Rect xpTab{panel.x+12, panel.y+50, 70, 28};
    const Rect hyperTab{panel.x+89, panel.y+50, 86, 28};
    const Rect timeTab{panel.x+182, panel.y+50, 105, 28};
    const Rect damageTab{panel.x+294, panel.y+50, 83, 28};
    const Rect prevPage{panel.right()-99, panel.y+50, 26, 28};
    const Rect nextPage{panel.right()-39, panel.y+50, 26, 28};
    const Rect startTrial{panel.right()-113, panel.y+50, 100, 28};

    const Rect closeRect = closeButtonRect(panel);
    const Rect refreshRect{panel.right() - 140.0, panel.y + 10.0, 80.0, 30.0};
    // Inside the card's border on three sides, so neither the rows nor the
    // track ever paint over the frame.
    const double bodyTop = kHeaderHeight + (damage_ ? 0.0 : 28.0);
    const Rect view{panel.x + kMenuBorder, panel.y + bodyTop, panel.w - kMenuBorder * 2,
                    std::max(0.0, panel.h - bodyTop - kMenuBorder)};
    const Rect bar{view.right() - kScrollbarWidth, view.y, kScrollbarWidth, view.h};

    // An empty board still reserves a row's worth of height, which is what
    // keeps its one line of prose from being the only thing that can scroll.
    scroll_.contentHeight = rowCount == 0 && !pending
                                ? kRowHeight
                                : kColumnHeader + static_cast<double>(rowCount) * kRowHeight;
    if (damage_) {
        scroll_.contentHeight = 2 * kColumnHeader;
        for (const auto& row : practice.own) scroll_.contentHeight += practiceRowHeight(row);
        for (const auto& row : practice.published) scroll_.contentHeight += practiceRowHeight(row);
        if (practice.published.empty()) scroll_.contentHeight += kRowHeight;
    }
    // Clamped against the viewport ui::scrollbar measures, not against the
    // clipped view, so the thumb lands on the end of its track exactly when
    // the board does; at the end of a long board that last 5px of content
    // sits under the card's border.
    scroll_.viewHeight = view.h + kTrackBottomInset;
    const bool scrollable = scroll_.contentHeight > scroll_.viewHeight;

    // Positive wheel delta means up: move toward the first row.
    if (panel.contains(mouse)) scroll_.offset -= ctx.wheel() * kWheelStep;
    // A finger is not inverted: the board follows it. Everything but the
    // track, which a finger drags the other way.
    scroll_.offset -=
        touchScroll(ctx.window, Rect{view.x, view.y, bar.x - view.x, view.h}, scrollable);

    // Press, not release: the browser panel acts on mousedown, and a press
    // that reaches the track is one the two buttons did not take.
    BarDrag& drag = barDrag();
    bool closing = false;
    if (ctx.pressed()) {
        if (closeRect.contains(mouse)) {
            closing = true;
        } else if (xpTab.contains(mouse) || hyperTab.contains(mouse) ||
                   timeTab.contains(mouse) || damageTab.contains(mouse)) {
            playtime_ = timeTab.contains(mouse);
            hyper_ = hyperTab.contains(mouse);
            damage_ = damageTab.contains(mouse);
            scroll_.offset = 0;
            drag.active = false;
            if (damage_) ctx.net.requestPracticeBoard(ctx.settings.showAdminsOnLeaderboard);
            else ctx.net.requestLeaderboard(ctx.settings.showAdminsOnLeaderboard);
            return true;
        } else if (hyper_ && !pending && (prevPage.contains(mouse) || nextPage.contains(mouse))) {
            const auto offset = ctx.net.hyperHoldingsOffset();
            if (prevPage.contains(mouse) && offset > 0)
                ctx.net.requestLeaderboard(ctx.settings.showAdminsOnLeaderboard, offset-kHyperBoardPageSize);
            if (nextPage.contains(mouse) && offset+kHyperBoardPageSize<ctx.net.hyperHoldingsTotal())
                ctx.net.requestLeaderboard(ctx.settings.showAdminsOnLeaderboard, offset+kHyperBoardPageSize);
            scroll_.offset = 0;
        } else if (damage_ && !pending && startTrial.contains(mouse)) {
            ctx.net.requestPracticeBoard(ctx.settings.showAdminsOnLeaderboard, net::PracticeBoardAction::Start);
        } else if (refreshRect.contains(mouse)) {
            if (damage_) ctx.net.requestPracticeBoard(ctx.settings.showAdminsOnLeaderboard);
            else ctx.net.requestLeaderboard(ctx.settings.showAdminsOnLeaderboard,
                hyper_ ? ctx.net.hyperHoldingsOffset() : 0);
        } else if (scrollable && bar.contains(mouse)) {
            drag.active = true;
            drag.anchorY = mouse.y;
            drag.anchorScroll = scroll_.offset;
        }
    }
    if (!ctx.window.mouseDown(MouseButton::Left)) drag.active = false;
    if (drag.active) {
        // Anywhere in the track column starts the drag and the content follows
        // the pointer from there; the thumb is never snapped under the cursor.
        const double ratio = (mouse.y - drag.anchorY) / (panel.h - kThumbTravelInset);
        scroll_.offset = drag.anchorScroll + ratio * scroll_.maxOffset();
    }
    scroll_.offset = clamp(scroll_.offset, 0.0, scroll_.maxOffset());

    if (closing) return false;

    panelCard(canvas, panel, kLeaderboardSkin);

    panelHeading(canvas, panel, menuTitle(MenuId::Leaderboard));

    // The count of every account the server holds, beside the title. It comes
    // with the board rather than being derived from it: the board is the top
    // 50 and this is the whole table. The active-today half only ever arrives
    // for an admin, which is how the browser's payload omits the field.
    if (ctx.net.totalAccounts() > 0) {
        TextStyle stats;
        stats.size = 13.0;
        stats.baseline = Baseline::Top;
        stats.strokeWidth = 0.0;
        std::string line = std::to_string(ctx.net.totalAccounts()) + " accounts";
        if (ctx.net.dailyActiveUsers() > 0) {
            // U+00B7, a middle dot, not a hyphen.
            line += " · " + std::to_string(ctx.net.dailyActiveUsers()) + " active today";
        }
        textAtAlpha(canvas, line, panel.x + 160.0, panel.y + 25.0, stats, 0.7);
    }

    pillButton(canvas, refreshRect, tipRefresh(), kLeaderboardSkin.border);
    panelClose(canvas, closeRect, closeRect.contains(mouse));
    pillButton(canvas, xpTab, "等级榜", !hyper_ && !playtime_ && !damage_ ? 0x85530Cu : kLeaderboardSkin.border);
    pillButton(canvas, hyperTab, "超凡持有", hyper_ ? 0x85530Cu : kLeaderboardSkin.border);
    pillButton(canvas, timeTab, "有效游玩时间", playtime_ ? 0x85530Cu : kLeaderboardSkin.border);
    pillButton(canvas, damageTab, "伤害统计", damage_ ? 0x85530Cu : kLeaderboardSkin.border);
    if (!damage_) {
        const auto rank = ctx.net.ownLeaderboardRank(playtime_, hyper_);
        const std::string status = pending ? "查询中" : rank ? "第 " + std::to_string(rank) + " 名" : "未上榜";
        TextStyle info;
        info.size = 15;
        text(canvas, "我的排行  " + status, panel.x + 15.0, panel.y + 103.0, info);
    }
    if (hyper_) {
        const auto pages = std::max(1u, (ctx.net.hyperHoldingsTotal()+kHyperBoardPageSize-1)/kHyperBoardPageSize);
        TextStyle info; info.size=12; info.strokeWidth=0;
        text(canvas, std::to_string(ctx.net.hyperHoldingsOffset()/kHyperBoardPageSize+1)+" / "+std::to_string(pages),
            panel.right()-70, panel.y+64, info);
        pillButton(canvas, prevPage, "<", kLeaderboardSkin.border);
        pillButton(canvas, nextPage, ">", kLeaderboardSkin.border);
    }

    if (damage_) pillButton(canvas, startTrial, "10 秒测试", kLeaderboardSkin.border);

    canvas.save();
    canvas.beginPath();
    canvas.roundRect(static_cast<float>(view.x), static_cast<float>(view.y),
                     static_cast<float>(view.w), static_cast<float>(view.h), 8.0f);
    canvas.clip();

    double y = view.y - scroll_.offset;

    if (damage_) {
        TextStyle label;
        label.size = 14.0;
        label.baseline = Baseline::Middle;
        const double left = panel.x + 18.0;
        const double right = panel.right() - 28.0;
        const auto fit = [&](const std::string& value, double maxWidth, double size) {
            return std::min(size, size * maxWidth / std::max(1.0, measure(value, size)));
        };
        const std::string header = pending ? "正在读取历史成绩…" :
            practice.ownRank == 0 ? "我的历史 · 到丛林超级木桩开始测试" :
            "我的历史 · 总排名 #" + std::to_string(practice.ownRank);
        text(canvas, header, left, y + 15.0, label);
        y += kColumnHeader;
        const auto draw = [&](const auto& entries, bool own) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& row = entries[i];
                const double top = y;
                y += practiceRowHeight(row);
                if (y < view.y || top > view.bottom()) continue;
                setFill(canvas, kPaper, i % 2 == 0 ? 0.15 : 0.08);
                canvas.fillRect(static_cast<float>(panel.x + 10), static_cast<float>(top),
                                static_cast<float>(panel.w - 20), static_cast<float>(y - top));
                label.align = Align::Left;
                label.fill = own ? kPaper : rankColor(static_cast<int>(i) + 1);
                const auto name = own ? (row.published ? "已公开" : "私密配装") :
                    std::to_string(i + 1) + ". " + row.name;
                label.size = fit(name, (right - left) * 0.48, 15.0);
                text(canvas, name, left, top + 12.0, label);
                label.size = 11.0;
                label.fill = kPaper;
                textAtAlpha(canvas, practiceDate(row.atMillis), left, top + 29.0, label, 0.75);
                const Rect share{right - 72.0, top + 7.0, 72.0, 24.0};
                const double scoreRight = own ? share.x - 9.0 : right;
                label.align = Align::Right;
                label.size = 14.0;
                text(canvas, formatCompact(row.damage / net::kPracticeWindowSeconds) + " DPS",
                     scoreRight, top + 18.0, label);
                if (own) {
                    pillButton(canvas, share, row.published ? "设为私密" : "公开", kLeaderboardSkin.border);
                    if (!pending && ctx.pressed() && view.contains(mouse) && share.contains(mouse))
                        ctx.net.requestPracticeBoard(ctx.settings.showAdminsOnLeaderboard,
                            row.published ? net::PracticeBoardAction::Hide : net::PracticeBoardAction::Publish, row.id);
                }
                const double half = (right - left) * 0.5;
                label.align = Align::Left;
                for (std::size_t j = 0; j < row.loadout.size(); ++j) {
                    const auto& item = row.loadout[j];
                    const auto& petal = content().petal(content().petalIndex(item.petal));
                    const auto itemLabel = std::to_string(item.slot) + ". " + rarityTr(item.rarity) +
                        " " + petalNameTr(item.petal, petal.name.c_str());
                    label.size = fit(itemLabel, half - 9.0, 13.0);
                    text(canvas, itemLabel, left + (j % 2) * half, top + kRowHeight + 18.0 * (j / 2) + 8.0, label);
                }
            }
        };
        draw(practice.own, true);
        label.align = Align::Left;
        label.size = 14.0;
        label.fill = kPaper;
        text(canvas, "公开配装榜 · 超级木桩 10 秒", left, y + 15.0, label);
        y += kColumnHeader;
        if (practice.published.empty() && !pending)
            text(canvas, "还没有玩家公开配装。", left, y + 15.0, label);
        draw(practice.published, false);
    } else if (rowCount == 0) {
        TextStyle empty;
        empty.size = 14.0;
        empty.align = Align::Centre;
        empty.baseline = Baseline::Top;
        empty.strokeWidth = 0.0;
        textAtAlpha(canvas, pending ? "加载中…" : (hyper_ ? "暂无超凡持有记录" : "暂无账号"),
                    panel.x + panel.w * 0.5, y + 20.0, empty, 0.7);
    } else {
        const double rankX = panel.x + 15.0;
        const double nameX = panel.x + 60.0;
        const double levelX = panel.right() - 170.0;
        const double xpX = panel.right() - 30.0;

        TextStyle header;
        header.size = 14.0;
        header.baseline = Baseline::Top;
        header.strokeWidth = 0.0;
        textAtAlpha(canvas, hyper_ ? "总H" : "#", rankX, y + 8.0, header, 0.8);
        textAtAlpha(canvas, "玩家", nameX, y + 8.0, header, 0.8);
        textAtAlpha(canvas, hyper_ ? "花瓣" : (playtime_ ? "" : "等级"), levelX, y + 8.0, header, 0.8);
        header.align = Align::Right;
        textAtAlpha(canvas, hyper_ ? "数量" : (playtime_ ? "活跃小时" : "XP"), xpX, y + 8.0, header, 0.8);
        y += kColumnHeader;

        for (std::size_t i = 0; i < rowCount; ++i) {
            const Rect row{panel.x + 10.0, y, panel.w - 20.0, kRowHeight};
            y += kRowHeight;
            if (row.bottom() < view.y || row.y > view.bottom()) continue;

            // Alternating bands. Both are white at low alpha rather than one
            // being the panel colour, so the stripe survives any panel hue.
            setFill(canvas, kPaper, (i % 2 == 0) ? 0.15 : 0.08);
            canvas.fillRect(static_cast<float>(row.x), static_cast<float>(row.y),
                            static_cast<float>(row.w), static_cast<float>(row.h));

            const int rank = static_cast<int>(i) + 1;
            const double middle = row.y + kRowHeight * 0.5;

            TextStyle cell;
            cell.size = 16.0;
            cell.fill = rankColor(rank);
            text(canvas, std::to_string(hyper_ ? holdings[i].ownerTotal : rank), rankX, middle, cell);

            cell.size = 14.0;
            const auto& name = hyper_ ? holdings[i].name : rows[i].name;
            cell.size = std::min(14.0, 14.0 * (levelX-nameX-12) / std::max(1.0, measure(name,14.0)));
            text(canvas, name, nameX, middle, cell);

            // The level travels with the row; the panel does not re-derive it
            // from XP with a curve that could drift from the server's.
            cell.fill = kPaper;
            const auto label = hyper_ ? petalNameTr(holdings[i].petal, holdings[i].petal.c_str()) : (playtime_ ? std::string() : std::to_string(rows[i].level));
            cell.size = std::min(14.0, 14.0 * (xpX-levelX-35) / std::max(1.0, measure(label,14.0)));
            text(canvas, label, levelX, middle, cell);

            cell.align = Align::Right;
            cell.size = 13.0;
            const auto hours = playtime_ ? std::to_string(static_cast<std::uint64_t>(rows[i].totalXp / 3600)) + "h " +
                std::to_string(static_cast<unsigned>(rows[i].totalXp / 60) % 60) + "m" : std::string();
            textAtAlpha(canvas, hyper_ ? std::to_string(holdings[i].count) : (playtime_ ? hours : formatXp(rows[i].totalXp)), xpX, middle, cell, 0.8);
        }
    }
    canvas.restore();

    if (scrollable) {
        scrollbar(canvas, view, scroll_.contentHeight, scroll_.offset, kLeaderboardSkin.accent,
                  kScrollbarWidth);
    }
    return true;
}

bool WavePanel::render(MenuContext& ctx) {
    const Rect panel = ctx.bounds;
    const Rect close = closeButtonRect(panel);
    const double x = panel.x + 16.0;
    const double y = panel.y;
    const Rect start{x, y + 94.0, 100.0, 30.0};
    const Rect leave{panel.right() - 116.0, y + 94.0, 100.0, 30.0};
    const Rect field{x, y + 140.0, panel.w - 135.0, 32.0};
    const Rect join{field.right() + 8.0, field.y, 94.0, field.h};
    const Vec2 mouse = ctx.mouse();
    if (ctx.pressed() && close.contains(mouse)) return false;

    trackTextMouse(ctx.window, field_, field, inputFieldRun(field, code_, field_),
                   code_, ctx.timeSeconds);
    if (ctx.timeSeconds >= nextStatusRequest_) {
        ctx.net.requestWaveRoomStatus();
        nextStatusRequest_ = ctx.timeSeconds + 2.5;
    }
    const bool typing = field_.focused;
    if (typing) {
        TextEditOptions options;
        options.maxBytes = 6;
        options.asciiOnly = true;
        editText(ctx.window, code_, field_, ctx.timeSeconds, options);
        ctx.wantsText = true;
    }
    const bool valid = code_.size() == 6 &&
        std::all_of(code_.begin(), code_.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (ctx.pressed() && start.contains(mouse)) {
        ctx.net.sendChat("/wave start");
        return false;
    }
    if (ctx.pressed() && leave.contains(mouse)) {
        ctx.net.sendChat("/wave leave");
        return false;
    }
    if (valid && ((ctx.pressed() && join.contains(mouse)) ||
                  (typing && ctx.window.keyPressed(Key::Enter)))) {
        ctx.net.sendChat("/wave join " + code_);
        return false;
    }

    Canvas& canvas = ctx.canvas;
    panelCard(canvas, panel, kLeaderboardSkin);
    panelHeading(canvas, panel, "Wave 试炼");
    panelClose(canvas, close, close.contains(mouse));
    TextStyle info;
    info.size = 14.0;
    const auto& status = ctx.net.waveRoomStatus;
    const auto lamp = [&](double left, const std::string& label, std::uint32_t colour) {
        fillQuadRound(canvas, {left, y + 49.0, 10.0, 10.0}, 5.0, colour);
        text(canvas, label, left + 16.0, y + 54.0, info);
    };
    lamp(x, status.known ? "房间 " + std::to_string(status.occupied) + "/4" : "查询房间中",
         !status.known ? 0x9BA4AAu : status.occupied == 4 ? 0xF47776u :
         status.occupied > 0 ? 0xFAD66Fu : 0x8EE26Au);
    lamp(panel.x + panel.w * .46,
         !status.known ? "查询奖励中" : status.dailyCapReached ? "今日星星已达上限" : "今日还可获得星星",
         !status.known ? 0x9BA4AAu : status.dailyCapReached ? 0xFAD66Fu : 0x8EE26Au);
    text(canvas, "房号发到个人聊天，开房 30 秒后开打。", x, y + 74.0, info);
    pillButton(canvas, start, "创建房间", kLeaderboardSkin.border);
    pillButton(canvas, leave, "离开房间", kLeaderboardSkin.border);
    inputField(canvas, field, code_, "六位房号", typing, ctx.timeSeconds, &field_);
    pillButton(canvas, join, "加入", valid ? kLeaderboardSkin.border : 0x767676u);
    text(canvas, "怪物有掉落，通关有星星，奖励波可领限时锤。", x, y + 195.0, info);
    return true;
}

} // namespace flix
