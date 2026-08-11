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
    JobRepository job_repo_;

    /// Current jobs
    std::unordered_map<JobId, Job> jobs_;

    /// Mutex for this job queue
    std::mutex mut_;

    /// Mutex for repository - pqxx connections are not thread safe
    std::mutex repo_mut_;

    /// condition variable
    std::condition_variable cv_;

    /// Shutdown. Use to kill all threads when program terminate
    bool shutdown_ = false;

public:
    explicit JobService(JobRepository const&);

    JobId Enqueue(const std::string& payload);
    std::optional<Job> Get(const JobId& id);
    std::vector<Job> GetAll();
    bool Remove(const JobId& id);
    std::optional<JobId> WaitAndPop();
    void Shutdown();
    void Finish(JobId const&);
};
