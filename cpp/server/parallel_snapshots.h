#pragma once

#include <memory>
#include "server/replication.h"

namespace flix {

// The world and event queue are frozen for the entire build call. Only each
// job's private view/output may change. Socket IO remains on the caller.
class ParallelSnapshots {
public:
    struct Job {
        net::ConnectionId connection = 0;
        Entity viewer = NULL_ENTITY;
        ClientView* view = nullptr;
        Replicator::Frame frame;
        std::vector<Entity> squad;
        ByteWriter output;
    };
    explicit ParallelSnapshots(unsigned workers = 3);
    ~ParallelSnapshots();
    ParallelSnapshots(const ParallelSnapshots&) = delete;
    ParallelSnapshots& operator=(const ParallelSnapshots&) = delete;
    void build(World& world, std::vector<Job>& jobs);
    unsigned workers() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flix
