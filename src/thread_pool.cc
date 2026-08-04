#include "thread_pool.h"

#include <chrono>

void ThreadPool::WorkerLoop() {
    while (!done_) {
        auto id = job_service_.WaitAndPop();

        // shutdown = true
        if (!id.has_value()) return;

        Execute(id.value());
    }
}

// pseudo-execute
// TODO: Change to real execute
void ThreadPool::Execute(JobId job_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    job_service_.Finish(job_id);
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