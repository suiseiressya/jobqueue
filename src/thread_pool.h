#include <thread>
#include <vector>
#include <atomic>

#include "job_service.h"

class ThreadPool {
private:
    std::vector<std::thread> workers_;
    JobService& job_service_;
    std::atomic_bool done_{false};
    size_t num_workers_;

    void WorkerLoop();
    void Execute(JobId);

public:
    ThreadPool(JobService& service, size_t thread_count = std::thread::hardware_concurrency()): 
        job_service_(service), num_workers_(thread_count) {}
    
    void Start();
    void Stop();
};