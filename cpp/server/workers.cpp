#include "server/workers.h"

#include <algorithm>
#include <cstdlib>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten.h>
EM_JS(int, flix_worker_env, (const char* name, char* out, int capacity), {
    if (typeof process === 'undefined') return 0;
    const value = process.env[UTF8ToString(name)];
    if (typeof value !== 'string' || lengthBytesUTF8(value) >= capacity) return 0;
    stringToUTF8(value, out, capacity);
    return 1;
});
#endif

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define FLIX_SERVER_THREADS 1
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>
#endif

namespace flix {

struct ServerWorkers::Impl {
#ifdef FLIX_SERVER_THREADS
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable ready, done;
    std::atomic<std::size_t> next{0};
    const Job* job = nullptr;
    std::size_t size = 0;
    unsigned generation = 0, active = 0, remaining = 0;
    bool stopping = false;

    void drain(unsigned lane) {
        for (std::size_t i = lane; i < size;
             i = next.fetch_add(1, std::memory_order_relaxed))
            (*job)(i, lane);
    }

    void worker(unsigned lane) {
        unsigned seen = 0;
        for (;;) {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return stopping || (lane <= active && generation != seen); });
            if (stopping) return;
            seen = generation;
            lock.unlock();
            drain(lane);
            lock.lock();
            if (--remaining == 0) done.notify_one();
        }
    }
#endif
};

ServerWorkers::ServerWorkers(unsigned count) : impl_(std::make_unique<Impl>()) {
#ifdef FLIX_SERVER_THREADS
    count = std::min(count, kMaxWorkers);
    for (unsigned lane = 1; lane <= count; ++lane) {
        try {
            impl_->threads.emplace_back([this, lane] { impl_->worker(lane); });
        } catch (const std::system_error&) {
            break;
        }
    }
#else
    (void)count;
#endif
}

ServerWorkers::~ServerWorkers() {
#ifdef FLIX_SERVER_THREADS
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->ready.notify_all();
    for (auto& thread : impl_->threads) thread.join();
#endif
}

unsigned ServerWorkers::count() const {
#ifdef FLIX_SERVER_THREADS
    return static_cast<unsigned>(impl_->threads.size());
#else
    return 0;
#endif
}

void ServerWorkers::run(std::size_t jobs, unsigned limit, const Job& job) {
#ifdef FLIX_SERVER_THREADS
    const unsigned active = static_cast<unsigned>(std::min<std::size_t>(
        std::min(count(), limit), jobs == 0 ? 0 : jobs - 1));
    if (active > 0) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->active = impl_->remaining = active;
            impl_->size = jobs;
            impl_->job = &job;
            impl_->next.store(active + 1, std::memory_order_relaxed);
            ++impl_->generation;
        }
        impl_->ready.notify_all();
        impl_->drain(0);
        std::unique_lock lock(impl_->mutex);
        impl_->done.wait(lock, [&] { return impl_->remaining == 0; });
        impl_->active = 0;
        impl_->job = nullptr;
        return;
    }
#else
    (void)limit;
#endif
    for (std::size_t i = 0; i < jobs; ++i) job(i, 0);
}

unsigned ServerWorkers::available() {
#ifdef FLIX_SERVER_THREADS
    return std::clamp(std::thread::hardware_concurrency(), 1u, kMaxWorkers + 1) - 1;
#else
    return 0;
#endif
}

unsigned ServerWorkers::configuredLimit(const char* name, unsigned defaultLimit) {
    const unsigned ceiling = available();
#if defined(__EMSCRIPTEN_PTHREADS__)
    char setting[64];
    const char* value = flix_worker_env(name, setting, sizeof setting) ? setting : nullptr;
#else
    const char* value = std::getenv(name);
#endif
    if (value) {
        char* end = nullptr;
        const long requested = std::strtol(value, &end, 10);
        if (end != value && *end == '\0' && requested >= 0 &&
            requested <= static_cast<long>(kMaxWorkers))
            return std::min(ceiling, static_cast<unsigned>(requested));
    }
    return std::min(ceiling, defaultLimit);
}

} // namespace flix
