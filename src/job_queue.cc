#include "job_queue.h"

/**
Push a new JobId into queue.
@param id: JobId
*/
void JobQueue::Push(const JobId& id) {
    pending_queue_.push_back(id);
}

/**
Retrieve the first JobId in the queue.
@return Empty if empty queue, else front of pending_queue_
*/
std::optional<JobId> JobQueue::Top() const {
    if (pending_queue_.empty()) return {};
    return pending_queue_.front();
}

/**
Pop the first JobId in the queue.
@return Empty if empty queue, else Top() if succesful
*/
std::optional<JobId> JobQueue::Pop() {
    auto top = JobQueue::Top();
    if (!top.has_value()) return {};

    JobId id = *top;
    pending_queue_.pop_front();
    return id;
}