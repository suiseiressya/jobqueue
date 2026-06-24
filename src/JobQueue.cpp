#include "JobQueue.hpp"

/**
Push a new JobId into queue. 
@param id: JobId
*/
void JobQueue::push(const JobId& id) {
    if (JobQueue::contains(id)) return;

    _readyQueue.push_back(id);
    _activeIds.insert(id);
}

/**
Retrieve the first JobId in the queue.
@return Empty if empty queue, else front of readyQueue
*/
std::optional<JobId> JobQueue::top() const {
    if (_readyQueue.empty()) return {};

    return _readyQueue.front();
}

/**
Pop the first JobId in the queue. 
This will NOT remove from _activeIds - since jobs might go active. 
To remove jobs after job done, use remove().

@return Empty if empty queue, else top() if succesful
*/
std::optional<JobId> JobQueue::pop() {
    auto top = JobQueue::top();
    if (!top.has_value()) return {};

    JobId id = *top;
    _readyQueue.pop_front();
    return id;
}

/**
Remove a JobId from _activeIds. Only use when a job is done.
*/
void JobQueue::remove(const JobId& id) {
    _activeIds.erase(id);
}
