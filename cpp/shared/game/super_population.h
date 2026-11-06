#pragma once
#include <algorithm>
#include <vector>
namespace flix {
/// Tracks vacancies only. It never selects or removes a living monster.
class SuperPopulation {
public:
    static constexpr int target = 10;
    static constexpr double respawnMillis = 30000.0;
    SuperPopulation(int capacity = target, double cooldownMillis = respawnMillis)
        : capacity_(std::max(1, capacity)), cooldownMillis_(std::max(respawnMillis, cooldownMillis)) {}
    void observe(int living, double now) {
        living = std::max(0, living);
        const int vacancies = std::max(0, capacity_ - living);
        if (!initialized_) {
            deadlines_.assign(static_cast<std::size_t>(vacancies), now);
            initialized_ = true;
        } else {
            for (int lost = std::max(0, previous_ - living); lost > 0; --lost)
                deadlines_.push_back(now + cooldownMillis_);
            std::sort(deadlines_.begin(), deadlines_.end());
            if (static_cast<int>(deadlines_.size()) > vacancies)
                deadlines_.resize(static_cast<std::size_t>(vacancies));
        }
        previous_ = living;
    }
    bool ready(double now) const {
        return !deadlines_.empty() && deadlines_.front() <= now && previous_ < capacity_;
    }
    void spawned() {
        if (deadlines_.empty()) return;
        deadlines_.erase(deadlines_.begin());
        ++previous_;
    }
private:
    int capacity_ = target;
    double cooldownMillis_ = respawnMillis;
    bool initialized_ = false;
    int previous_ = 0;
    std::vector<double> deadlines_;
};
}
