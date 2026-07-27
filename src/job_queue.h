#pragma once
#include <deque>
#include <optional>
#include <unordered_set>

#include "job_id.h"

/**
JobQueue class. Acts as a simple queue system to schedule jobs.
Does not have locking / mutex capabilities - only meant to be called via a JobService.
*/
class JobQueue {
private:
    /// Current queue for pending jobs.
    std::deque<JobId> ready_queue_;

    /// Ids of current active jobs. Jobs that are active may be in active_ids_
    /// but not in ready_queue_ since ready_queue_ only store pending jobs, while
    // active_ids_ also store active jobs
    std::unordered_set<JobId> active_ids_;

public:
    void Push(const JobId& id);
    std::optional<JobId> Top() const;
    std::optional<JobId> Pop();
    void Remove(const JobId& id);
    bool Contains(const JobId& id) const {
        return active_ids_.find(id) != active_ids_.end();
    }
    size_t Size() const {
        return ready_queue_.size();
    }
};
