#include "server/parallel_snapshots.h"

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define FLIX_PARALLEL_SNAPSHOTS 1
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace flix {

struct ParallelSnapshots::Impl {
    Replicator serial;
#ifdef FLIX_PARALLEL_SNAPSHOTS
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable ready, done;
    std::atomic<std::size_t> next{0};
    World* world = nullptr;
    std::vector<Job>* jobs = nullptr;
    unsigned generation = 0, remaining = 0;
    bool stopping = false;

    void drain(Replicator& replicator, std::size_t first) {
        auto i = first;
        for (;;) {
            if (i >= jobs->size()) return;
            auto& job = (*jobs)[i];
            replicator.build(*world, job.viewer, *job.view, job.frame, job.output);
            i = next.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void worker(unsigned lane) {
        Replicator replicator;
        unsigned seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&] { return stopping || generation != seen; });
            if (stopping) return;
            seen = generation;
            lock.unlock();
            drain(replicator, lane);
            lock.lock();
            if (--remaining == 0) done.notify_one();
        }
    }
#endif
};

ParallelSnapshots::ParallelSnapshots(unsigned workers) : impl_(std::make_unique<Impl>()) {
    Replicator::prepareParallelReads();
#ifdef FLIX_PARALLEL_SNAPSHOTS
    workers = std::min(workers, 3u);
    for (unsigned i = 0; i < workers; ++i)
        impl_->threads.emplace_back([this, i] { impl_->worker(i); });
#else
    (void)workers;
#endif
}

ParallelSnapshots::~ParallelSnapshots() {
#ifdef FLIX_PARALLEL_SNAPSHOTS
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->ready.notify_all();
    for (auto& thread : impl_->threads) thread.join();
#endif
}

unsigned ParallelSnapshots::workers() const {
#ifdef FLIX_PARALLEL_SNAPSHOTS
    return static_cast<unsigned>(impl_->threads.size());
#else
    return 0;
#endif
}

void ParallelSnapshots::build(World& world, std::vector<Job>& jobs) {
    // Assign pointers only after the caller finishes growing the job vector.
    for (auto& job : jobs) {
        job.output.clear();
        job.frame.alwaysVisible = job.squad.empty() ? nullptr : &job.squad;
    }
#ifdef FLIX_PARALLEL_SNAPSHOTS
    if (!impl_->threads.empty() && jobs.size() >= 4) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->world = &world;
            impl_->jobs = &jobs;
            impl_->next.store(impl_->threads.size() + 1, std::memory_order_relaxed);
            impl_->remaining = static_cast<unsigned>(impl_->threads.size());
            ++impl_->generation;
        }
        impl_->ready.notify_all();
        impl_->drain(impl_->serial, impl_->threads.size());
        std::unique_lock<std::mutex> lock(impl_->mutex);
        impl_->done.wait(lock, [&] { return impl_->remaining == 0; });
        impl_->world = nullptr;
        impl_->jobs = nullptr;
        return;
    }
#endif
    for (auto& job : jobs)
        impl_->serial.build(world, job.viewer, *job.view, job.frame, job.output);
}

} // namespace flix
