#include "client/ui/mobile_controls.h"

#include <algorithm>
#include <cmath>

#include "client/ui/draw.h"
#include "client/ui/text.h"

namespace flix::ui {

namespace {

// Every measurement here is the browser build's own (src/graphics/
// mobile-controls.ts, in git history), times kTouchScale. The reference's
// value is kept beside it so the two can be read against each other: a retune
// is a change to the number on the left, never to the arithmetic.

/// Gap above the loadout bar's footprint that all controls hang from.
constexpr double kBottomGap = 15.0 * kTouchScale;
/// The stick's centre, from the left edge. Past the bottom-left icon column
/// (which reaches x = 73) rather than at the natural margin + radius, so the
/// two do not overlap -- the reference shifts its own stick right for the same
/// reason.
constexpr double kStickCentreX = 150.0 * kTouchScale;
/// Extra lift, on top of the loadout bar's clearance.
///
/// The chat column is 195 units tall in the bottom-left corner, directly
/// behind the stick, and clearing the loadout bar alone still leaves the stick
/// standing on the transcript. It has to clear the column by its HIT radius
/// rather than its drawn one, or a press aimed at a line of chat lands in the
/// stick's slack and steers instead of selecting; 195 - 142.75 - 36 + 0.3 x
/// 132 is where that lands, and this is the next round number above it.
///
/// The reference lifts its own stick past its own chat box by 60 CSS pixels.
/// That the two agree is a coincidence of two different layouts -- what is
/// shared is the reason. Attack / Retract / R need no lift at all: they are
/// far enough right to be clear of the column's width.
constexpr double kStickLift = 60.0;
constexpr double kStickRadius = 55.0 * kTouchScale;
constexpr double kKnobRadius = 26.0 * kTouchScale;
/// Fingers are imprecise and the stick is the control that must never be
/// missed, so it answers for a ring wider than it draws.
constexpr double kStickHitRadius = kStickRadius * 1.3;
/// As a fraction of the base radius. Under it the stick reads as centred.
constexpr double kStickDeadZone = 0.12;

constexpr double kAttackRadius = 46.0 * kTouchScale;
constexpr double kAttackMargin = 30.0 * kTouchScale;
constexpr double kRetractRadius = 34.0 * kTouchScale;
constexpr double kRetractGap = 16.0 * kTouchScale;
/// Same face as Retract: R is a second key-stand-in, not a combat button.
constexpr double kSwapRadius = kRetractRadius;
constexpr double kSwapGap = kRetractGap;
/// Both buttons take presses slightly outside their face, for the same reason
/// the stick does.
constexpr double kButtonHitSlack = 1.2;

/// Clear air between the stick's answering ring and the nearest button's, and
/// between the stick and the bottom-left icon column (which reaches x = 73).
/// Only ever reached on a viewport too narrow to lay the row out at their
/// natural positions.
constexpr double kControlGap = 8.0;
constexpr double kIconColumnRight = 73.0;

constexpr double kRingWidth = 3.0 * kTouchScale;
constexpr double kGlyphWidth = 4.0 * kTouchScale;

// rgba(220,60,60) / rgba(255,120,120) and rgba(60,110,220) / rgba(120,170,255),
// which is what the reference fills and strokes the two combat buttons with.
constexpr std::uint32_t kAttackFill = 0xDC3C3Cu;
constexpr std::uint32_t kAttackRing = 0xFF7878u;
constexpr std::uint32_t kRetractFill = 0x3C6EDCu;
constexpr std::uint32_t kRetractRing = 0x78AAFFu;
/// Swap / presets: warm amber so it reads apart from Attack and Retract.
constexpr std::uint32_t kSwapFill = 0xC48A2Au;
constexpr std::uint32_t kSwapRing = 0xE8C06Au;

bool within(Vec2 centre, double x, double y, double radius) {
    const double dx = x - centre.x;
    const double dy = y - centre.y;
    return dx * dx + dy * dy <= radius * radius;
}

void circle(Canvas& canvas, Vec2 centre, double radius) {
    canvas.beginPath();
    canvas.arc(static_cast<float>(centre.x), static_cast<float>(centre.y),
               static_cast<float>(radius), 0.0f, static_cast<float>(kTau));
}

} // namespace

void MobileControls::layout(int viewWidth, int viewHeight, double loadoutBarHeight) {
    placed_ = true;

    // Everything shrinks together on a viewport too narrow to lay the row
    // out side by side -- a phone held upright, where the loadout bar does not
    // fit either and nothing about this HUD is happy. Shrinking is what is
    // left: the alternative is the stick sitting underneath a button, where
    // one finger lands on two controls and a hit test decides which one it
    // drives.
    //
    // The width a full-size row asks for, left to right: the icon column it
    // has to clear, air, the stick's answering ring, air, and everything from
    // the right edge in to the leftmost point the buttons answer for (R, then
    // Retract, then Attack).
    constexpr double kNeeded = kIconColumnRight + 2 * kControlGap + 2 * kStickHitRadius +
                               kAttackMargin + 2 * kAttackRadius + kRetractGap +
                               kRetractRadius + kSwapGap + kSwapRadius +
                               kSwapRadius * kButtonHitSlack;
    scale_ = std::min(1.0, viewWidth / kNeeded);

    const double bottom = viewHeight - loadoutBarHeight - kBottomGap * scale_;
    stickCentre_ = {kStickCentreX * scale_,
                    bottom - (kStickRadius + kStickLift) * scale_};
    attackCentre_ = {viewWidth - (kAttackMargin + kAttackRadius) * scale_,
                     bottom - kAttackRadius * scale_};
    retractCentre_ = {attackCentre_.x - (kAttackRadius + kRetractGap + kRetractRadius) * scale_,
                      attackCentre_.y};
    swapCentre_ = {retractCentre_.x - (kRetractRadius + kSwapGap + kSwapRadius) * scale_,
                   retractCentre_.y};

    // The stick's own x is a fraction of the design width rather than of this
    // window's, so a narrow one has to place it against the room that is
    // actually left instead. Down to a floor that keeps it off the icons.
    const double roomFor = swapCentre_.x -
                           (kSwapRadius * kButtonHitSlack + kStickHitRadius) * scale_ -
                           kControlGap;
    const double floorX = kIconColumnRight + kControlGap + kStickHitRadius * scale_;
    stickCentre_.x = std::min(stickCentre_.x, std::max(floorX, roomFor));
}

double MobileControls::stickHitRadius() const {
    return placed_ ? kStickHitRadius * scale_ : 0.0;
}

std::array<Rect, 4> MobileControls::hitBounds() const {
    if (!placed_) return {};
    const auto bounds = [](Vec2 centre, double radius) {
        return Rect{centre.x - radius, centre.y - radius, 2 * radius, 2 * radius};
    };
    Rect stick = bounds(stickCentre_, stickHitRadius());
    stick.h = stickApproachBottom() - stick.y;
    return {stick, bounds(swapCentre_, kSwapRadius * kButtonHitSlack * scale_),
            bounds(retractCentre_, kRetractRadius * kButtonHitSlack * scale_),
            bounds(attackCentre_, kAttackRadius * kButtonHitSlack * scale_)};
}

bool MobileControls::hits(double x, double y) const {
    if (!placed_) return false;
    const double stickR = kStickHitRadius * scale_;
    if (within(stickCentre_, x, y, stickR) ||
        within(attackCentre_, x, y, kAttackRadius * kButtonHitSlack * scale_) ||
        within(retractCentre_, x, y, kRetractRadius * kButtonHitSlack * scale_) ||
        within(swapCentre_, x, y, kSwapRadius * kButtonHitSlack * scale_)) {
        return true;
    }
    // Leave a short approach below the joystick without claiming the chat
    // row below it.
    return x >= stickCentre_.x - stickR && x <= stickCentre_.x + stickR &&
           y >= stickCentre_.y && y <= stickApproachBottom();
}

double MobileControls::stickApproachBottom() const {
    return stickCentre_.y + stickHitRadius() + 18.0 * scale_;
}

void MobileControls::update(const std::vector<TouchEvent>& events, double timeSeconds) {
    if (!placed_) return;

    // Long-press fires even when the finger is still -- no Moved event arrives
    // while a thumb rests on R, and the threshold has to be checked every
    // frame against the clock.
    if (swap_.held && !swapLong_ && swapDownAt_ >= 0.0 &&
        timeSeconds - swapDownAt_ >= kSwapLongPressSeconds) {
        swapLong_ = true;
    }

    for (const TouchEvent& event : events) {
        const TouchPoint& point = event.point;
        switch (event.phase) {
            case TouchPhase::Began:
                // First free control the finger is over, in left-to-right
                // combat order after the stick. A finger that lands on a
                // control already held belongs to nothing: the second thumb on
                // one button is not a second press of it.
                if (!stick_.held && hits(point.x, point.y) &&
                    !within(attackCentre_, point.x, point.y,
                            kAttackRadius * kButtonHitSlack * scale_) &&
                    !within(retractCentre_, point.x, point.y,
                            kRetractRadius * kButtonHitSlack * scale_) &&
                    !within(swapCentre_, point.x, point.y,
                            kSwapRadius * kButtonHitSlack * scale_)) {
                    // Stick circle OR the approach pad under it — same claim
                    // as hits(), minus the combat buttons.
                    stick_ = {point.id, true};
                    moveKnob(point.x, point.y);
                } else if (!attack_.held && within(attackCentre_, point.x, point.y,
                                                   kAttackRadius * kButtonHitSlack * scale_)) {
                    attack_ = {point.id, true};
                } else if (!retract_.held && within(retractCentre_, point.x, point.y,
                                                    kRetractRadius * kButtonHitSlack * scale_)) {
                    retract_ = {point.id, true};
                } else if (!swap_.held && within(swapCentre_, point.x, point.y,
                                                 kSwapRadius * kButtonHitSlack * scale_)) {
                    swap_ = {point.id, true};
                    swapDownAt_ = timeSeconds;
                    swapLong_ = false;
                }
                break;

            case TouchPhase::Moved:
                // Only the stick follows a finger. A thumb that slides off a
                // button keeps holding it, which is what a thumb resting on a
                // fire button does all game.
                if (stick_.is(point.id)) moveKnob(point.x, point.y);
                break;

            case TouchPhase::Ended:
                if (stick_.is(point.id)) {
                    stick_.held = false;
                    knob_ = {0, 0};
                }
                if (attack_.is(point.id)) attack_.held = false;
                if (retract_.is(point.id)) retract_.held = false;
                if (swap_.is(point.id)) {
                    // A short release is the row swap (or cancels a sticky L
                    // bank MenuSystem already armed). A long one only ends the
                    // hold -- the sticky bank stays until a tile is picked or
                    // R is tapped again.
                    if (!swapLong_) swapTapPending_ = true;
                    swap_.held = false;
                    swapDownAt_ = -1.0;
                    swapLong_ = false;
                }
                break;
        }
    }
}

bool MobileControls::consumeSwapRowsTap() {
    const bool tap = swapTapPending_;
    swapTapPending_ = false;
    return tap;
}

void MobileControls::reset() {
    stick_.held = false;
    attack_.held = false;
    retract_.held = false;
    swap_.held = false;
    swapDownAt_ = -1.0;
    swapLong_ = false;
    swapTapPending_ = false;
    knob_ = {0, 0};
}

void MobileControls::moveKnob(double x, double y) {
    const Vec2 offset{x - stickCentre_.x, y - stickCentre_.y};
    const double distance = offset.length();
    // Past the base the knob stops at the rim and the direction goes on
    // turning: a thumb that overshoots is still steering, not letting go.
    const double base = kStickRadius * scale_;
    knob_ = distance <= base ? offset : offset * (base / distance);
}

bool MobileControls::stick(Stick& out) const {
    if (!stick_.held) return false;
    const double distance = knob_.length();
    const double base = kStickRadius * scale_;
    if (distance <= base * kStickDeadZone) return false;
    out.direction = knob_ * (1.0 / distance);
    out.magnitude = std::min(1.0, distance / base);
    return true;
}

void MobileControls::draw(Canvas& canvas) const {
    canvas.save();
    canvas.setLineCap("round");
    canvas.setLineJoin("round");

    // --- the stick ---------------------------------------------------------
    circle(canvas, stickCentre_, kStickRadius * scale_);
    setFill(canvas, kPaper, 0.15);
    canvas.fill();
    canvas.setLineWidth(static_cast<float>(kRingWidth * scale_));
    setStroke(canvas, kPaper, 0.35);
    canvas.stroke();

    circle(canvas, {stickCentre_.x + knob_.x, stickCentre_.y + knob_.y}, kKnobRadius * scale_);
    setFill(canvas, kPaper, stick_.held ? 0.55 : 0.4);
    canvas.fill();

    // --- the buttons -------------------------------------------------------
    const auto button = [&](Vec2 centre, double radius, std::uint32_t fill,
                            std::uint32_t ring, bool pressed) {
        circle(canvas, centre, radius);
        setFill(canvas, fill, 0.55);
        canvas.fill();
        canvas.setLineWidth(static_cast<float>(kRingWidth * scale_));
        setStroke(canvas, ring, 0.9);
        canvas.stroke();
        // A press is a wash over the face, not a different fill: the button
        // has to stay the colour it was while a thumb is covering most of it.
        if (!pressed) return;
        circle(canvas, centre, radius);
        setFill(canvas, kPaper, 0.25);
        canvas.fill();
    };

    button(attackCentre_, kAttackRadius * scale_, kAttackFill, kAttackRing, attackPressed());
    button(retractCentre_, kRetractRadius * scale_, kRetractFill, kRetractRing,
           retractPressed());
    button(swapCentre_, kSwapRadius * scale_, kSwapFill, kSwapRing,
           swap_.held);

    canvas.setLineWidth(static_cast<float>(kGlyphWidth * scale_));
    setStroke(canvas, kPaper, 0.9);

    // Attack: eight spokes flying outward, which is petals extending.
    const double burst = kAttackRadius * scale_ * 0.5;
    canvas.beginPath();
    for (int i = 0; i < 8; ++i) {
        const double angle = (i / 8.0) * kTau;
        canvas.moveTo(static_cast<float>(attackCentre_.x + std::cos(angle) * burst * 0.35),
                      static_cast<float>(attackCentre_.y + std::sin(angle) * burst * 0.35));
        canvas.lineTo(static_cast<float>(attackCentre_.x + std::cos(angle) * burst),
                      static_cast<float>(attackCentre_.y + std::sin(angle) * burst));
    }
    canvas.stroke();

    // Retract: two chevrons pointing at each other.
    const double chevron = kRetractRadius * scale_ * 0.5;
    canvas.beginPath();
    for (const double side : {-1.0, 1.0}) {
        canvas.moveTo(static_cast<float>(retractCentre_.x + side * chevron),
                      static_cast<float>(retractCentre_.y - chevron * 0.6));
        canvas.lineTo(static_cast<float>(retractCentre_.x + side * chevron * 0.3),
                      static_cast<float>(retractCentre_.y));
        canvas.lineTo(static_cast<float>(retractCentre_.x + side * chevron),
                      static_cast<float>(retractCentre_.y + chevron * 0.6));
    }
    canvas.stroke();

    // R: the bound Swap loadout rows key. A letter rather than another glyph
    // so it matches the caption under the loadout bar.
    TextStyle letter;
    letter.size = std::round(kSwapRadius * scale_ * 0.85);
    letter.fill = kPaper;
    letter.stroke = kInk;
    letter.align = Align::Centre;
    letter.baseline = Baseline::Middle;
    text(canvas, "R", swapCentre_.x, swapCentre_.y, letter);

    canvas.restore();
}

} // namespace flix::ui
