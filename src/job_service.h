#pragma once
#include <condition_variable>
#include <mutex>
#include <unordered_map>

#include "job_queue.h"
#include "job.h"
#include "job_repository.h"

/**
JobService class.
Service layer, wrapped around a JobQueue. Manage current jobs and locking.
*/
class JobService {
private:
    /// Underlying job queue
    JobQueue job_queue_;

    /// Repository 
    JobRepository& job_repo_;

    /// Cache of current jobs 
    /// Map JobId -> Job
    std::unordered_map<JobId, Job> job_cache_;

    /// Mutex for this job queue
    std::mutex mut_;

    /// condition variable
    std::condition_variable cv_;

    /// Shutdown. Use to kill all threads when program terminate
    bool shutdown_ = false;

public:
    explicit JobService(JobRepository&);

    JobId Enqueue(const std::string& payload);
    std::optional<Job> GetById(const JobId& id);
    std::vector<Job> GetAll();
    std::optional<Job> WaitAndPop();
    void Shutdown();
    void Finish(JobId const&);
    void RetryThenFail(JobId const&, std::string const& error);
    /// Scheduler tick: queue due PENDING rows not already cached (D-016, D-019).
    /// Returns how many were queued.
    size_t EnqueueDueJobs();
    size_t RestartRecovery();

    /// Number of jobs in the local cache (queued or running). For tests.
    size_t CacheSize();
    bool CancelPendingJobById(JobId const&);
};
