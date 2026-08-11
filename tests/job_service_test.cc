#include "job_service.h"

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("JobService enqueue creates job with correct state") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};

    auto id = svc.Enqueue("test-payload");

    auto job = svc.GetById(id);
    REQUIRE(job.has_value());
    REQUIRE(job->payload == "test-payload");
    REQUIRE(job->job_status == kPending);
    REQUIRE(job->retry_count == 0);
    REQUIRE(job->id == id);
}

TEST_CASE("JobService get returns nullopt for unknown id") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    auto id = JobId::Generate();
    REQUIRE_FALSE(svc.GetById(id).has_value());
}

TEST_CASE("JobService getAll returns all enqueued jobs") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    REQUIRE(svc.GetAll().empty());

    svc.Enqueue("a");
    svc.Enqueue("b");
    svc.Enqueue("c");

    auto all = svc.GetAll();
    REQUIRE(all.size() == 3);

    std::set<std::string> payloads;
    for (const auto& j : all) payloads.insert(j.payload);
    REQUIRE(payloads.count("a"));
    REQUIRE(payloads.count("b"));
    REQUIRE(payloads.count("c"));
}

TEST_CASE("JobService remove deletes job") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    auto id = svc.Enqueue("to-delete");

    REQUIRE(svc.Remove(id));
    REQUIRE_FALSE(svc.GetById(id).has_value());
    REQUIRE(svc.GetAll().empty());
}

TEST_CASE("JobService remove returns false for unknown id") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    REQUIRE_FALSE(svc.Remove(JobId::Generate()));
}

TEST_CASE("JobService each enqueue generates unique id") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    std::set<std::string> ids;

    for (int i = 0; i < 100; i++) {
        auto id = svc.Enqueue("job-" + std::to_string(i));
        ids.insert(id.ToString());
    }

    REQUIRE(ids.size() == 100);
}

// --- Thread safety tests (for TSan / Helgrind) ---

TEST_CASE("JobService concurrent enqueue") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    constexpr int kNumThreads = 8;
    constexpr int kJobsPerThread = 1000;

    std::vector<std::thread> threads;
    std::vector<std::vector<JobId>> results(kNumThreads);

    for (int t = 0; t < kNumThreads; t++) {
        threads.emplace_back([&svc, &results, t] {
            for (int i = 0; i < kJobsPerThread; i++) {
                results[t].push_back(
                    svc.Enqueue("t" + std::to_string(t) + "-" + std::to_string(i)));
            }
        });
    }

    for (auto& th : threads) th.join();

    auto all = svc.GetAll();
    REQUIRE(all.size() == kNumThreads * kJobsPerThread);

    std::set<std::string> unique_ids;
    for (const auto& j : all) unique_ids.insert(j.id.ToString());
    REQUIRE(unique_ids.size() == kNumThreads * kJobsPerThread);
}

TEST_CASE("JobService concurrent enqueue and get") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    constexpr int kNumOps = 1000;

    std::vector<std::thread> threads;

    threads.emplace_back([&svc] {
        for (int i = 0; i < kNumOps; i++) {
            svc.Enqueue("writer-" + std::to_string(i));
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < kNumOps; i++) {
            svc.GetAll();
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < kNumOps; i++) {
            svc.GetById(JobId::Generate());
        }
    });

    for (auto& th : threads) th.join();

    REQUIRE(svc.GetAll().size() == kNumOps);
}

TEST_CASE("JobService concurrent enqueue and remove") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    constexpr int kNumJobs = 1000;

    std::vector<JobId> ids;
    for (int i = 0; i < kNumJobs; i++) {
        ids.push_back(svc.Enqueue("job-" + std::to_string(i)));
    }

    std::vector<std::thread> threads;

    threads.emplace_back([&svc] {
        for (int i = 0; i < 50; i++) {
            svc.Enqueue("extra-" + std::to_string(i));
        }
    });

    threads.emplace_back([&svc, &ids] {
        for (int i = 0; i < 100; i++) {
            svc.Remove(ids[i]);
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < 100; i++) {
            svc.GetAll();
        }
    });

    for (auto& th : threads) th.join();

    auto remaining = svc.GetAll();
    REQUIRE(remaining.size() == (kNumJobs - 100 + 50));
}
