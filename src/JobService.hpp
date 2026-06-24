#pragma once
#include <mutex>
#include <unordered_map>

#include "JobQueue.hpp"
#include "Job.hpp"

/**
JobService class.
Service layer, wrapped around a JobQueue. Manage current jobs and locking. 
*/
class JobService {
private:
    /// Underlying job queue
    JobQueue _jobQueue;

    /// Current jobs
    std::unordered_map<JobId, Job> _jobs;

    /// Mutex for this job queue
    std::mutex _queueMutex;

public:
    JobId enqueue(const std::string& payload);
    std::optional<Job> get(const JobId& id);
    std::vector<Job> getAll();
    bool remove(const JobId& id);
};
