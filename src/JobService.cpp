#include "JobService.hpp"
#include "Job.hpp"

/**
Creates a new job from payload. 
@param payload: payload string
@return JobId of newly created job
*/
JobId JobService::enqueue(const std::string& payload) {
    std::lock_guard guard(_queueMutex);

    JobId jobId = JobId::generate();
    Job job = Job(jobId, payload);

    _jobs[jobId] = job;
    _jobQueue.push(jobId);
    _cv.notify_one();

    return jobId;
}

/**
Get a job based on its JobId. 
@return std::optional<Job>: empty if no job found, else the Job associated with JobId
*/
std::optional<Job> JobService::get(const JobId& id) {
    std::lock_guard guard(_queueMutex);

    auto it = _jobs.find(id);
    if (it == _jobs.end()) return {};
    return it->second;
}

/**
Get all current jobs.
@return vector<Job> of all jobs
*/
std::vector<Job> JobService::getAll() {
    std::lock_guard guard(_queueMutex);

    std::vector<Job> results;
    for (const auto& [k, v] : _jobs) {
        results.push_back(v);
    }

    return results;
}

/**
Remove a job based on JobId. 
@return true if successful, false if not
*/
bool JobService::remove(const JobId& id) {
    std::lock_guard guard(_queueMutex);
    return _jobs.erase(id) > 0;
}

std::optional<JobId> JobService::wait_and_pop() {
    std::unique_lock lock(_queueMutex);

    _cv.wait(lock, [&] {
        return _shutdown || _jobQueue.top().has_value();
    });

    if (_shutdown) return {};

    JobId jobId = _jobQueue.pop().value();
    _jobs[jobId].status = RUNNING;
    lock.unlock();

    return jobId;
}

void JobService::shutdown() {
    std::lock_guard guard(_queueMutex);

    _shutdown = true;
    _cv.notify_all();
}