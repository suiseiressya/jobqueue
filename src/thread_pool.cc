#include "thread_pool.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using Json = nlohmann::json;

/**
 * Simulate work 
 */
void SimulateWork(const std::string& payload) {
    Json json = Json::parse(payload);
    if (!json.is_object()) throw std::invalid_argument("not JSON");

    // Sleep to simulate work
    int sleep_ms = json.value("sleep_ms", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

    if (json.value("fail", false)) 
        throw std::runtime_error("payload requested failure");
}

void ThreadPool::WorkerLoop() {
    while (!done_) {
        auto job = job_service_.WaitAndPop();

        // shutdown = true
        if (!job.has_value()) return;

        // keep jobs running even if failed, 
        try {
            Execute(job.value());
        } catch (const std::exception& e) {
            std::cerr << job->id.ToString() << "failed: " << e.what() << std::endl;
        }
    }
}

/**
 * Execute the job. 
 * Two outcomes: Fail() or Finish(). Both get reported to the DB and then removed 
 * from current job cache. 
 * If Fail() or Finish() throw, will get up to WorkerLoop(). 
 */
void ThreadPool::Execute(Job const& job) {
    try {
        SimulateWork(job.payload);
    } catch (const std::exception& e) {
        std::cerr << job.id.ToString() << " failed: " << e.what() << std::endl;
        job_service_.Fail(job.id);
        return;
    }

    job_service_.Finish(job.id);
}

void ThreadPool::Start() {
    for (size_t i = 0; i < num_workers_; i++) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

void ThreadPool::Stop() {
    job_service_.Shutdown();

    done_ = true;
    for (auto& worker : workers_)
        worker.join();
}