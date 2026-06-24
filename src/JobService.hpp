#pragma once
#include <mutex>
#include <unordered_map>

#include "JobQueue.hpp"
#include "Job.hpp"

class JobService {
private:
    JobQueue _jobQueue;
    std::unordered_map<JobId, Job> _jobs;
    std::mutex _queueMutex;

public:
    JobId enqueue(const std::string& payload);
    std::optional<Job> get(const JobId& id);
    std::vector<Job> getAll();
};
