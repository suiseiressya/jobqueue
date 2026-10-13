#pragma once
#include <deque>
#include <optional>

#include "job_id.h"

/**
JobQueue class. Acts as a simple queue system to schedule jobs.
Does not have locking / mutex capabilities - only meant to be called via a JobService.
*/
class JobQueue {
private:
    /// Current queue for pending jobs.
    std::deque<JobId> pending_queue_;

public:
    void Push(const JobId& id);
    std::optional<JobId> Top() const;
    std::optional<JobId> Pop();
    size_t Size() const {
        return pending_queue_.size();
    }
};
