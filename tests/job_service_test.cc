#include "job_service.h"

#include "test_db_helpers.h"
#include "test_log_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("JobService enqueue creates job with correct state") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
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
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    auto id = JobId::Generate();
    REQUIRE_FALSE(svc.GetById(id).has_value());
}

TEST_CASE("JobService getAll returns all enqueued jobs") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
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

TEST_CASE("JobService each enqueue generates unique id") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
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
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    constexpr int kNumThreads = 8;
    constexpr int kJobsPerThread = 100;

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
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};
    constexpr int kNumOps = 100;

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

// --- Cache lifecycle: a job leaves job_cache_ once it ends ---

TEST_CASE("Finish marks job DONE in DB and removes it from the cache") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto id = svc.Enqueue("{}");
    REQUIRE(svc.CacheSize() == 1);

    auto job = svc.WaitAndPop();
    REQUIRE(job.has_value());
    REQUIRE(job->id == id);
    REQUIRE(svc.CacheSize() == 1);  // running jobs stay cached

    svc.Finish(id);
    REQUIRE(test_db::StatusOf(conn, id) == "DONE");
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("Fail marks job FAILED in DB and removes it from the cache") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto id = svc.Enqueue("{}");
    REQUIRE(svc.WaitAndPop().has_value());

    svc.Fail(id);
    REQUIRE(test_db::StatusOf(conn, id) == "FAILED");
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("Finish on a row that is no longer RUNNING leaves DB alone but still uncaches") {
    test_log::ExpectFailureLogs(1);
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto id = svc.Enqueue("{}");
    REQUIRE(svc.WaitAndPop().has_value());

    // Someone else moved the row on (affected_rows() == 0 path).
    {
        pqxx::work txn(conn);
        txn.exec_params("UPDATE jobs SET job_status = 'FAILED' WHERE id = $1", id.ToString());
        txn.commit();
    }

    svc.Finish(id);
    REQUIRE(test_db::StatusOf(conn, id) == "FAILED");  // conditional update did not overwrite
    REQUIRE(svc.CacheSize() == 0);
}

// --- Restart recovery (D-010) ---

TEST_CASE("RestartRecovery resets RUNNING, reloads PENDING in created_at order") {
    pqxx::connection conn{test_db::kConn};
    
    // Write straight into DB without JobService to stage a crash.
    auto oldest_pending = test_db::InsertJob(conn, "p-oldest", "PENDING", "3 minutes");
    auto running = test_db::InsertJob(conn, "r-middle", "RUNNING", "2 minutes");
    auto newest_pending = test_db::InsertJob(conn, "p-newest", "PENDING", "1 minute");
    auto done = test_db::InsertJob(conn, "done", "DONE", "4 minutes");
    auto failed = test_db::InsertJob(conn, "failed", "FAILED", "5 minutes");

    JobRepository repo{conn};
    JobService svc{repo};

    REQUIRE(svc.RestartRecovery() == 3);
    REQUIRE(svc.CacheSize() == 3);

    // DB: no RUNNING left, terminal rows untouched.
    REQUIRE(test_db::CountWithStatus(conn, "RUNNING") == 0);
    REQUIRE(test_db::StatusOf(conn, running) == "PENDING");
    REQUIRE(test_db::StatusOf(conn, done) == "DONE");
    REQUIRE(test_db::StatusOf(conn, failed) == "FAILED");

    // Queue order follows created_at, regardless of the status the row had before.
    auto first = svc.WaitAndPop();
    auto second = svc.WaitAndPop();
    auto third = svc.WaitAndPop();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(third.has_value());
    REQUIRE(first->id == oldest_pending);
    REQUIRE(second->id == running);
    REQUIRE(third->id == newest_pending);
    REQUIRE(second->payload == "r-middle");
}

TEST_CASE("RestartRecovery on an empty DB recovers nothing") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    REQUIRE(svc.RestartRecovery() == 0);
    REQUIRE(svc.CacheSize() == 0);
}

// --- Cancel: DB first, then cache (I-003) ---

TEST_CASE("CancelPendingJobById deletes a queued job from DB and cache") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto id = svc.Enqueue("{}");
    REQUIRE(svc.CacheSize() == 1);

    REQUIRE(svc.CancelPendingJobById(id));
    REQUIRE_FALSE(svc.GetById(id).has_value());
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("CancelPendingJobById returns false for an unknown id") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    REQUIRE_FALSE(svc.CancelPendingJobById(JobId::Generate()));
}

TEST_CASE("CancelPendingJobById refuses a RUNNING job and keeps it cached") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto id = svc.Enqueue("{}");
    REQUIRE(svc.WaitAndPop().has_value());  // now RUNNING

    REQUIRE_FALSE(svc.CancelPendingJobById(id));
    REQUIRE(test_db::StatusOf(conn, id) == "RUNNING");
    REQUIRE(svc.CacheSize() == 1);  // worker still owns it
}

TEST_CASE("WaitAndPop skips a cancelled job left in the queue") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    auto cancelled = svc.Enqueue("{}");
    auto kept = svc.Enqueue("{}");
    REQUIRE(svc.CancelPendingJobById(cancelled));

    // `cancelled` is still at the front of the deque (lazy deletion); it must be skipped.
    auto job = svc.WaitAndPop();
    REQUIRE(job.has_value());
    REQUIRE(job->id == kept);
}

TEST_CASE("A cancelled job is not brought back by RestartRecovery") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    {
        JobService before_crash{repo};
        auto id = before_crash.Enqueue("{}");
        REQUIRE(before_crash.CancelPendingJobById(id));
    }

    JobService after_restart{repo};
    REQUIRE(after_restart.RestartRecovery() == 0);
    REQUIRE(after_restart.CacheSize() == 0);
}
