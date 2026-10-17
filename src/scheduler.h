#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "job_service.h"

class Scheduler {
private:
    JobService& service_;
    std::chrono::milliseconds interval_;
    std::mutex mut_;               
    std::condition_variable cv_;
    std::thread t_;
    bool stop_ = false;

    void Loop();

public:
    Scheduler(JobService& service, std::chrono::milliseconds interval)
        : service_(service), interval_(interval) {}
    ~Scheduler() { Stop(); } 

    void Start();
    void Stop();
};
