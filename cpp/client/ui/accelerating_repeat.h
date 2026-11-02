#pragma once

#include <algorithm>
#include <cmath>

namespace flix::ui {

/// One immediate step, then accelerated repeats while the same arrow is held.
/// Releasing, dragging off, changing station or closing the panel cancels it.
struct AcceleratingRepeat {
    int direction = 0;
    double started = 0;
    double next = 0;

    void reset() { direction = 0; }
    int update(int hovered, bool pressed, bool down, double now) {
        if (!down || hovered != direction) reset();
        if (pressed && hovered != 0) {
            direction = hovered;
            started = now;
            next = now + 0.35;
            return direction;
        }
        if (direction == 0 || now < next) return 0;
        const double held = std::max(0.0, now - started);
        // 5 steps/sec -> 20 steps/sec; longer holds also increase step size.
        next = now + std::max(0.05, 0.20 - held * 0.05);
        const int step = held >= 5.0 ? 100 : held >= 3.0 ? 10 : 1;
        return direction * step;
    }
};

} // namespace flix::ui
