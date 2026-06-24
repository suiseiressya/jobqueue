#include "JobService.hpp"
#include "Job.hpp"

JobId JobService::enqueue(const std::string& payload) {
    std::lock_guard guard(_queueMutex);

    JobId jobId = JobId::generate();
    Job job = Job(jobId, payload);

    _jobs[jobId] = job;
    _jobQueue.push(jobId);

    return jobId;
}

std::optional<Job> JobService::get(const JobId& id) {
    std::lock_guard guard(_queueMutex);

    auto it = _jobs.find(id);
    if (it == _jobs.end()) return {};
    return it->second;
}

std::vector<Job> JobService::getAll() {
    std::lock_guard guard(_queueMutex);

    std::vector<Job> results;
    for (const auto& [k, v] : _jobs) {
        results.push_back(v);
    }

    return results;
}

bool JobService::remove(const JobId& id) {
    std::lock_guard guard(_queueMutex);
    return _jobs.erase(id) > 0;
}
