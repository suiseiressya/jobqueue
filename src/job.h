#pragma once
#include <chrono>
#include <string>

#include "job_id.h"

enum Status { kPending, kRunning, kDone, kFailed };

/**
Job class.
Contains id, payload, status, created time, retry count
*/
struct Job {
    JobId id;
    std::string payload;
    Status job_status;
    std::chrono::system_clock::time_point created_at;
    int retry_count;

    Job() {}
    Job(JobId id, const std::string& payload, Status job_status = kPending)
        : id(id),
          payload(payload),
          job_status(job_status),
          created_at(std::chrono::system_clock::now()),
          retry_count(0) {}
};