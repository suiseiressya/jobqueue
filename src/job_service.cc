#include "job_service.h"

#include <cstddef>
#include <iostream>

JobService::JobService(JobRepository& job_repo) : job_repo_(job_repo) {}

/**
Creates a new job from payload.
@param payload: payload string
@return JobId of newly created job
*/
JobId JobService::Enqueue(const std::string& payload) {
    JobId job_id = JobId::Generate();
    Job job = Job(job_id, payload);
    job_repo_.CreateJob(job);

    {
        std::lock_guard guard(mut_);
        job_cache_[job_id] = job;
        job_queue_.Push(job_id);
        cv_.notify_one();
    }

    return job_id;
}

/**
Get a job based on its JobId.
@return std::optional<Job>: empty if no job found, else the Job associated with JobId
*/
std::optional<Job> JobService::GetById(const JobId& id) {
    auto job = job_repo_.GetById(id);

    return job;
}

/**
Get all current jobs.
@return vector<Job> of all jobs
*/
std::vector<Job> JobService::GetAll() {
    auto jobs = job_repo_.GetAllJobs();

    return jobs;
}

/**
Remove a job from cache based on JobId.
@return true if successful, false if not
*/
bool JobService::RemoveFromCache(const JobId& id) {
    std::lock_guard guard(mut_);

    return job_cache_.erase(id) > 0;
}

std::optional<Job> JobService::WaitAndPop() {
    std::unique_lock lock(mut_);

    while (true) {
        cv_.wait(lock, [&] {
            return shutdown_ || job_queue_.Top().has_value();
        });

        if (shutdown_) return {};

        JobId job_id = job_queue_.Pop().value();
        auto it = job_cache_.find(job_id);

        // if HTTP DELETE called and delete the job before it got popped out of queue 
        // then this job is stale 
        // in that case just remove from queue
        if (it == job_cache_.end()) continue;
        lock.unlock();

        size_t affected = job_repo_.UpdateJobStatus(job_id, kPending, kRunning);

        // if affected == 0: row is not PENDING between Pop() and UPDATE 
        // Caused by HTTP DELETE, which call CancelPendingJobById which send another call to DB 
        // DB will determine the race result; indeterministic
        if (affected == 0) {
            std::cerr << "[service] job " << job_id.ToString()
                      << ": PENDING -> RUNNING skipped, row no longer PENDING in DB "
                      << std::endl;
            lock.lock();
            continue;
        }

        lock.lock();
        it = job_cache_.find(job_id);
        it->second.job_status = kRunning;
        return it->second;  // copy: caller uses it without holding mut_
    }
}

void JobService::Shutdown() {
    std::lock_guard guard(mut_);

    shutdown_ = true;
    cv_.notify_all();
}

/**
Mark a job from kRunning to kDone in DB and remove from cache
*/
void JobService::Finish(JobId const& job_id) {
    size_t affected = job_repo_.UpdateJobStatus(job_id, kRunning, kDone);

    std::lock_guard guard(mut_);

    // If affected = 0, then DB not bugged and job is finished
    // No need to keep the stale job in cache, just delete it
    if (affected == 0)
        std::cerr << "[service] job " << job_id.ToString()
                  << ": RUNNING -> DONE skipped, DB row is no longer RUNNING; dropped from cache"
                  << std::endl;
    job_cache_.erase(job_id);
}

/**
Mark a job from kRunning to kFailed in DB and remove from cache
*/
void JobService::Fail(JobId const& job_id) {
    size_t affected = job_repo_.UpdateJobStatus(job_id, kRunning, kFailed);

    std::lock_guard guard(mut_);
    if (affected == 0)
        std::cerr << "[service] job " << job_id.ToString()
                  << ": RUNNING -> FAILED skipped, DB row is no longer RUNNING; dropped from cache"
                  << std::endl;
    job_cache_.erase(job_id);
}

/**
 * Restart recovery: Resume all stale jobs in DB 
 * after DB restarts
 */
size_t JobService::RestartRecovery() {
    job_repo_.ResetRunningToPending();
    std::vector<Job> recovered_jobs = job_repo_.GetPendingJobOrderByCreatedAt();

    std::unique_lock lock(mut_);

    for (auto& job: recovered_jobs) {
        job_cache_[job.id] = job;
        job_queue_.Push(job.id);
    }

    lock.unlock();
    cv_.notify_all();

    return recovered_jobs.size();
}

size_t JobService::CacheSize() {
    std::lock_guard guard(mut_);
    return job_cache_.size();
}

bool JobService::CancelPendingJobById(JobId const& id) {
    size_t n = job_repo_.CancelPendingJobById(id);
    if (n == 0) return false;
    
    std::lock_guard lock(mut_);
    job_cache_.erase(id);
    return true;
}