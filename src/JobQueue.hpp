#pragma once
#include <deque>
#include <optional>
#include <unordered_set>

#include "JobId.hpp"

/**
JobQueue class. Acts as a simple queue system to schedule jobs.
Does not have locking / mutex capabilities - only meant to be called via a JobService.
*/
class JobQueue {
private:
    /// Current queue for pending jobs. 
    std::deque<JobId> _readyQueue;

    /// Ids of current active jobs. Jobs that are active may be in _activeIds 
    /// but not in _readyQueue since _readyQueue only store pending jobs, while 
    // _activeIds also store active jobs
    std::unordered_set<JobId> _activeIds;

public:
    void push(const JobId& id);
    std::optional<JobId> top() const;
    std::optional<JobId> pop();
    void remove(const JobId& id);
    bool contains(const JobId& id) const {
        return _activeIds.find(id) != _activeIds.end();
    }
    size_t size() const {
        return _readyQueue.size();
    }
};
