#pragma once

#include "server/replication.h"
#include "server/workers.h"

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
    ParallelSnapshots(ServerWorkers& workers, unsigned limit);
    void build(World& world, std::vector<Job>& jobs);
private:
    ServerWorkers& workers_;
    unsigned limit_;
    std::vector<Replicator> lanes_;
};

} // namespace flix
