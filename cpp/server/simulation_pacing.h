#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace flix {

/// Leaves I/O time between simulation steps when the main thread is loaded.
/// The logical clock keeps all simulated deadlines on the same time scale.
class SimulationPacing {
public:
    int ticksPerSecond() const { return kRates[tier_]; }
    double intervalMillis() const { return 1000.0 / ticksPerSecond(); }
    double meanWorkMillis() const { return meanWorkMillis_; }

    double advance(double wallMillis) {
        if (!initialized_) {
            initialized_ = true;
            worldMillis_ = lastWallMillis_ = wallMillis;
            return worldMillis_;
        }
        const double elapsed = std::max(0.0, wallMillis - lastWallMillis_);
        lastWallMillis_ = wallMillis;
        worldMillis_ += tier_ == 0 ? elapsed : std::min(elapsed, kNominalMillis);
        return worldMillis_;
    }

    void observe(double wallMillis, double workMillis, bool enabled, bool playing) {
        if (!enabled || !playing) {
            tier_ = 0;
            meanWorkMillis_ = 0;
            pressureSince_ = recoverySince_ = -1;
            return;
        }
        meanWorkMillis_ += (workMillis - meanWorkMillis_) * 0.125;
        // At 20 Hz, isolated overruns and a busy but stable cadence should
        // not slow the world. In protection, preserve a small I/O window.
        const double limit = tier_ == 0 ? kNominalMillis + 4.0 : intervalMillis() - 4.0;
        if (tier_ + 1 < kRates.size() && meanWorkMillis_ >= limit) {
            if (pressureSince_ < 0) pressureSince_ = wallMillis;
            if (wallMillis - pressureSince_ >= 1800.0) {
                do { ++tier_; }
                while (tier_ + 1 < kRates.size() && meanWorkMillis_ + 8.0 > intervalMillis());
                pressureSince_ = recoverySince_ = -1;
            }
        } else {
            pressureSince_ = -1;
        }

        if (tier_ > 0 && canRecoverTo(tier_ - 1)) {
            if (recoverySince_ < 0) recoverySince_ = wallMillis;
            if (wallMillis - recoverySince_ >= 4500.0) {
                do { --tier_; }
                while (tier_ > 0 && canRecoverTo(tier_ - 1));
                pressureSince_ = recoverySince_ = -1;
            }
        } else {
            recoverySince_ = -1;
        }
    }

private:
    bool canRecoverTo(std::size_t tier) const {
        const double reserveMillis = tier == 0 ? 1.0 : 9.0;
        return meanWorkMillis_ + reserveMillis < 1000.0 / kRates[tier];
    }

    static constexpr std::array<int, 5> kRates{20, 16, 13, 11, 10};
    static constexpr double kNominalMillis = 1000.0 / kRates[0];
    std::size_t tier_ = 0;
    double meanWorkMillis_ = 0;
    double pressureSince_ = -1;
    double recoverySince_ = -1;
    bool initialized_ = false;
    double lastWallMillis_ = 0;
    double worldMillis_ = 0;
};

} // namespace flix
