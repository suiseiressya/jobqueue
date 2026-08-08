#include "job_service.h"
#include "job.h"

/**
Creates a new job from payload.
@param payload: payload string
@return JobId of newly created job
*/
JobId JobService::Enqueue(const std::string& payload) {
    std::lock_guard guard(mut_);

    JobId job_id = JobId::Generate();
    Job job = Job(job_id, payload);

    jobs_[job_id] = job;
    job_queue_.Push(job_id);
    cv_.notify_one();

    return job_id;
}

/**
Get a job based on its JobId.
@return std::optional<Job>: empty if no job found, else the Job associated with JobId
*/
std::optional<Job> JobService::Get(const JobId& id) {
    std::lock_guard guard(mut_);

    auto it = jobs_.find(id);
    if (it == jobs_.end()) return {};
    return it->second;
}

/**
Get all current jobs.
@return vector<Job> of all jobs
*/
std::vector<Job> JobService::GetAll() {
    std::lock_guard guard(mut_);

    std::vector<Job> results;
    for (const auto& [k, v] : jobs_) {
        results.push_back(v);
    }

    return results;
}

/**
Remove a job based on JobId.
@return true if successful, false if not
*/
bool JobService::Remove(const JobId& id) {
    std::lock_guard guard(mut_);

    job_queue_.Remove(id);
    return jobs_.erase(id) > 0;
}

std::optional<JobId> JobService::WaitAndPop() {
    std::unique_lock lock(mut_);

    while (true) {
        cv_.wait(lock, [&] {
            return shutdown_ || job_queue_.Top().has_value();
        });

        if (shutdown_) return {};

        JobId job_id = job_queue_.Pop().value();
        auto it = jobs_.find(job_id);

        // if job removed before being picked up: discard the stale job in queue 
        // and continue waiting instead
        if (it == jobs_.end()) {
            job_queue_.Remove(job_id);
            continue;
        }

        it->second.job_status = kRunning;
        lock.unlock();
        return job_id;
    }
}

void JobService::Shutdown() {
    std::lock_guard guard(mut_);

    shutdown_ = true;
    cv_.notify_all();
}

/**
Mark a job as done (finished). 
Still keep inside jobs_. TODO: decide final behavior
*/
void JobService::Finish(JobId const& job_id) {
    std::lock_guard guard(mut_);

    auto it = jobs_.find(job_id);
    if (it != jobs_.end()) it->second.job_status = kDone;
    job_queue_.Remove(job_id);
}