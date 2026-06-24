#pragma once
#include <chrono>
#include <string>

#include "JobId.hpp"

enum Status { PENDING, RUNNING, DONE, FAILED };

/**
Job class. 
Contains id, payload, status, created time, retry count
*/
struct Job {
    JobId id;
    std::string payload;
    Status status;
    std::chrono::system_clock::time_point created_at;
    int retry_count;

    Job() {}
    Job(JobId id, const std::string& payload)
        : id(id),
          payload(payload),
          status(PENDING),
          created_at(std::chrono::system_clock::now()),
          retry_count(0) {}
};