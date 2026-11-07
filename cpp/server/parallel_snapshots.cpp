#include "server/parallel_snapshots.h"

namespace flix {

ParallelSnapshots::ParallelSnapshots(ServerWorkers& workers, unsigned limit)
    : workers_(workers), limit_(std::min(limit, workers.count())), lanes_(limit_ + 1) {
    Replicator::prepareParallelReads();
}

void ParallelSnapshots::build(World& world, std::vector<Job>& jobs) {
    // Assign pointers only after the caller finishes growing the job vector.
    for (auto& job : jobs) {
        job.output.clear();
        job.frame.alwaysVisible = job.squad.empty() ? nullptr : &job.squad;
    }
    workers_.run(jobs.size(), jobs.size() >= 4 ? limit_ : 0,
                 [&](std::size_t i, unsigned lane) {
        auto& job = jobs[i];
        lanes_[lane].build(world, job.viewer, *job.view, job.frame, job.output);
    });
}

} // namespace flix
