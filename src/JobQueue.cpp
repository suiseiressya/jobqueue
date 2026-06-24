#include "JobQueue.hpp"

void JobQueue::push(const JobId& id) {
    if (JobQueue::contains(id)) return;

    _readyQueue.push_back(id);
    _activeIds.insert(id);
}

std::optional<JobId> JobQueue::top() const {
    if (_readyQueue.empty()) return {};

    return _readyQueue.front();
}

std::optional<JobId> JobQueue::pop() {
    auto top = JobQueue::top();
    if (!top.has_value()) return {};

    JobId id = *top;
    _readyQueue.pop_front();
    return id;
}

void JobQueue::remove(const JobId& id) {
    _activeIds.erase(id);
}
