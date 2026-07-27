#include "job_queue.h"

/**
Push a new JobId into queue.
@param id: JobId
*/
void JobQueue::Push(const JobId& id) {
    if (JobQueue::Contains(id)) return;

    ready_queue_.push_back(id);
    active_ids_.insert(id);
}

/**
Retrieve the first JobId in the queue.
@return Empty if empty queue, else front of ready_queue_
*/
std::optional<JobId> JobQueue::Top() const {
    if (ready_queue_.empty()) return {};

    return ready_queue_.front();
}

/**
Pop the first JobId in the queue.
This will NOT remove from active_ids_ - since jobs might go active.
To remove jobs after job done, use Remove().

@return Empty if empty queue, else Top() if succesful
*/
std::optional<JobId> JobQueue::Pop() {
    auto top = JobQueue::Top();
    if (!top.has_value()) return {};

    JobId id = *top;
    ready_queue_.pop_front();
    return id;
}

/**
Remove a JobId from active_ids_. Only use when a job is done.
*/
void JobQueue::Remove(const JobId& id) {
    active_ids_.erase(id);
}
