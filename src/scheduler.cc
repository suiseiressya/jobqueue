#include "scheduler.h"

#include <iostream>
#include <syncstream>

void Scheduler::Start() {
    t_ = std::thread([this] { Loop(); });
}

/**
 * Called in ~Scheduler(). 
 * Without this the scheduler thread will just hangs forever. 
 */
void Scheduler::Stop() {
    {
        std::lock_guard lock(mut_);
        stop_ = true;
    }
    cv_.notify_one();
    if (t_.joinable()) t_.join();
}

/**
 * Turn on and push all due jobs into the queue every interval_
 */
void Scheduler::Loop() {
    std::unique_lock lock(mut_);
    while (!stop_) {
        lock.unlock();
        try {
            service_.EnqueueDueJobs();
        } catch (std::exception const& e) {
            std::osyncstream(std::cerr) << "[scheduler] EnqueueDueJobs failed: " << e.what() << std::endl;
        }
        lock.lock();

        // either sleep until stop_ is true or interval_ time passed
        cv_.wait_for(lock, interval_, [this] { return stop_; });
    }
}
