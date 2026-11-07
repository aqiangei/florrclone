#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace flix {

// The caller participates as lane zero and waits for all jobs before returning.
// Jobs may use their lane for reusable scratch. Dispatch is never nested.
class ServerWorkers {
public:
    static constexpr unsigned kMaxWorkers = 15;
    using Job = std::function<void(std::size_t index, unsigned lane)>;

    explicit ServerWorkers(unsigned count);
    ~ServerWorkers();
    unsigned count() const;
    void run(std::size_t jobs, unsigned limit, const Job& job);

    static unsigned available();
    static unsigned configuredLimit(const char* name, unsigned defaultLimit = kMaxWorkers);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flix
