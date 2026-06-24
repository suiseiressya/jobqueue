#pragma once
#include <deque>
#include <optional>
#include <unordered_set>

#include "JobId.hpp"

class JobQueue {
private:
    std::deque<JobId> _readyQueue;
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
