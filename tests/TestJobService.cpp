#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <thread>
#include <vector>

#include "JobService.hpp"

TEST_CASE("JobService enqueue creates job with correct state") {
    JobService svc;
    auto id = svc.enqueue("test-payload");

    auto job = svc.get(id);
    REQUIRE(job.has_value());
    REQUIRE(job->payload == "test-payload");
    REQUIRE(job->status == PENDING);
    REQUIRE(job->retry_count == 0);
    REQUIRE(job->id == id);
}

TEST_CASE("JobService get returns nullopt for unknown id") {
    JobService svc;
    auto id = JobId::generate();
    REQUIRE_FALSE(svc.get(id).has_value());
}

TEST_CASE("JobService getAll returns all enqueued jobs") {
    JobService svc;
    REQUIRE(svc.getAll().empty());

    svc.enqueue("a");
    svc.enqueue("b");
    svc.enqueue("c");

    auto all = svc.getAll();
    REQUIRE(all.size() == 3);

    std::set<std::string> payloads;
    for (const auto& j : all) payloads.insert(j.payload);
    REQUIRE(payloads.count("a"));
    REQUIRE(payloads.count("b"));
    REQUIRE(payloads.count("c"));
}

TEST_CASE("JobService remove deletes job") {
    JobService svc;
    auto id = svc.enqueue("to-delete");

    REQUIRE(svc.remove(id));
    REQUIRE_FALSE(svc.get(id).has_value());
    REQUIRE(svc.getAll().empty());
}

TEST_CASE("JobService remove returns false for unknown id") {
    JobService svc;
    REQUIRE_FALSE(svc.remove(JobId::generate()));
}

TEST_CASE("JobService each enqueue generates unique id") {
    JobService svc;
    std::set<std::string> ids;

    for (int i = 0; i < 100; i++) {
        auto id = svc.enqueue("job-" + std::to_string(i));
        ids.insert(id.to_string());
    }

    REQUIRE(ids.size() == 100);
}

// --- Thread safety tests (for TSan / Helgrind) ---

TEST_CASE("JobService concurrent enqueue") {
    JobService svc;
    constexpr int numThreads = 8;
    constexpr int jobsPerThread = 1000;

    std::vector<std::thread> threads;
    std::vector<std::vector<JobId>> results(numThreads);

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&svc, &results, t] {
            for (int i = 0; i < jobsPerThread; i++) {
                results[t].push_back(svc.enqueue("t" + std::to_string(t) + "-" + std::to_string(i)));
            }
        });
    }

    for (auto& th : threads) th.join();

    auto all = svc.getAll();
    REQUIRE(all.size() == numThreads * jobsPerThread);

    std::set<std::string> uniqueIds;
    for (const auto& j : all) uniqueIds.insert(j.id.to_string());
    REQUIRE(uniqueIds.size() == numThreads * jobsPerThread);
}

TEST_CASE("JobService concurrent enqueue and get") {
    JobService svc;
    constexpr int numOps = 1000;

    std::vector<std::thread> threads;

    threads.emplace_back([&svc] {
        for (int i = 0; i < numOps; i++) {
            svc.enqueue("writer-" + std::to_string(i));
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < numOps; i++) {
            svc.getAll();
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < numOps; i++) {
            svc.get(JobId::generate());
        }
    });

    for (auto& th : threads) th.join();

    REQUIRE(svc.getAll().size() == numOps);
}

TEST_CASE("JobService concurrent enqueue and remove") {
    JobService svc;
    constexpr int numJobs = 1000;

    std::vector<JobId> ids;
    for (int i = 0; i < numJobs; i++) {
        ids.push_back(svc.enqueue("job-" + std::to_string(i)));
    }

    std::vector<std::thread> threads;

    threads.emplace_back([&svc] {
        for (int i = 0; i < 50; i++) {
            svc.enqueue("extra-" + std::to_string(i));
        }
    });

    threads.emplace_back([&svc, &ids] {
        for (int i = 0; i < 100; i++) {
            svc.remove(ids[i]);
        }
    });

    threads.emplace_back([&svc] {
        for (int i = 0; i < 100; i++) {
            svc.getAll();
        }
    });

    for (auto& th : threads) th.join();

    auto remaining = svc.getAll();
    REQUIRE(remaining.size() == (numJobs - 100 + 50));
}
