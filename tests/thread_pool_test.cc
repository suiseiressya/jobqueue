#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <string>
#include <thread>

#include "http_server.h"
#include "test_db_helpers.h"
#include "test_log_helpers.h"
#include "thread_pool.h"

using Json = nlohmann::json;

namespace {

// Same wiring as main.cc: HttpServer and ThreadPool share one JobService.
class ServerFixture {
public:
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService job_service_{job_repo_};
    HttpServer server_{job_service_};
    ThreadPool pool_{job_service_, /*thread_count=*/20};
    std::thread server_thread_;
    httplib::Client client_{"localhost", 9091};

    ServerFixture() {
        server_thread_ = std::thread([this] { server_.Listen("127.0.0.1", 9091); });
        client_.set_connection_timeout(2);
        while (!client_.Get("/jobs")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pool_.Start();
    }

    ~ServerFixture() {
        pool_.Stop();
        client_.stop();
        server_.Stop();
        if (server_thread_.joinable()) server_thread_.join();
    }
};

// Poll the DB until the job reaches DONE or FAILED, bounded by a timeout.
Status WaitForFinalStatus(JobService& svc, const JobId& id) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        auto job = svc.GetById(id);
        if (job && (job->job_status == kDone || job->job_status == kFailed)) {
            return job->job_status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return svc.GetById(id)->job_status;
}

}  // namespace

TEST_CASE("POST 100 jobs are all picked up and completed by the worker pool") {
    ServerFixture f;
    constexpr int kNumJobs = 100;

    std::set<std::string> ids;
    for (int i = 0; i < kNumJobs; i++) {
        // Payload is a JSON string holding the simulated work spec (D-015).
        std::string body = R"({"payload":"{\"sleep_ms\":)" + std::to_string(i % 5) + R"(}"})";
        auto res = f.client_.Post("/jobs", body, "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 201);
        ids.insert(Json::parse(res->body)["id"].get<std::string>());
    }
    REQUIRE(ids.size() == kNumJobs);

    // Worker pool processes asynchronously (100ms simulated work per job) —
    // poll GET /jobs until every job reaches DONE, bounded by a timeout so a
    // stuck pool fails the test instead of hanging it.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    size_t done_count = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto res = f.client_.Get("/jobs");
        REQUIRE(res);
        auto jobs = Json::parse(res->body);

        done_count = 0;
        for (const auto& job : jobs) {
            if (job["status"] == kDone) done_count++;
        }
        if (done_count == kNumJobs) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    REQUIRE(done_count == kNumJobs);
}

TEST_CASE("Removing a job before it is picked up keeps persisted job pending") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};

    // Enqueue and remove before any worker exists, so the id is guaranteed
    // to still be sitting in the queue (never popped) when Start() runs.
    auto id = svc.Enqueue("to-be-deleted");
    REQUIRE(svc.RemoveFromCache(id));
    auto job = svc.GetById(id);
    REQUIRE(job.has_value());
    REQUIRE(job->job_status == kPending);

    ThreadPool pool(svc, 4);
    pool.Start();

    // Removal drops this id from queue/cache. DB-backed reads must still show
    // persisted job, but worker pool must not change its status.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    job = svc.GetById(id);
    REQUIRE(job.has_value());
    REQUIRE(job->job_status == kPending);

    pool.Stop();
}

TEST_CASE("Execute marks job DONE when fake work succeeds") {
    pqxx::connection conn{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 2);
    pool.Start();

    auto id = svc.Enqueue(R"({"sleep_ms":10})");
    REQUIRE(WaitForFinalStatus(svc, id) == kDone);

    pool.Stop();
}

TEST_CASE("Execute marks job FAILED when payload requests failure") {
    test_log::ExpectFailureLogs(1);
    pqxx::connection conn{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 2);
    pool.Start();

    auto id = svc.Enqueue(R"({"fail":true})");
    REQUIRE(WaitForFinalStatus(svc, id) == kFailed);

    pool.Stop();
}

TEST_CASE("Invalid payload marks job FAILED and the worker keeps running") {
    test_log::ExpectFailureLogs(3);
    pqxx::connection conn{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository repo{conn};
    JobService svc{repo};
    // One worker, so the second job proves the same worker survived the first.
    ThreadPool pool(svc, 1);
    pool.Start();

    auto bad_json = svc.Enqueue("not json");
    auto not_object = svc.Enqueue("[1, 2]");
    auto bad_type = svc.Enqueue(R"({"sleep_ms":"ten"})");
    auto good = svc.Enqueue("{}");

    REQUIRE(WaitForFinalStatus(svc, bad_json) == kFailed);
    REQUIRE(WaitForFinalStatus(svc, not_object) == kFailed);
    REQUIRE(WaitForFinalStatus(svc, bad_type) == kFailed);
    REQUIRE(WaitForFinalStatus(svc, good) == kDone);

    pool.Stop();
}

TEST_CASE("Jobs left RUNNING by a crash are recovered and completed by the pool") {
    pqxx::connection conn{test_db::kConn};
    
    // Insert 2 Jobs into db to simulate a crash
    auto crashed = test_db::InsertJob(conn, R"({"sleep_ms":10})", "RUNNING", "1 minute");
    auto queued = test_db::InsertJob(conn, R"({"sleep_ms":10})", "PENDING", "30 seconds");

    JobRepository repo{conn};
    JobService svc{repo};
    REQUIRE(svc.RestartRecovery() == 2);

    ThreadPool pool(svc, 2);
    pool.Start();

    REQUIRE(WaitForFinalStatus(svc, crashed) == kDone);
    REQUIRE(WaitForFinalStatus(svc, queued) == kDone);
    REQUIRE(test_db::CountWithStatus(conn, "RUNNING") == 0);

    pool.Stop();
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("A cancelled job is never executed by the pool") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};

    // Cancel before any worker exists, so the stale id is guaranteed to be in the queue.
    auto cancelled = svc.Enqueue("{}");
    REQUIRE(svc.CancelPendingJobById(cancelled));

    ThreadPool pool(svc, 1);
    pool.Start();

    // The single worker must pop past the stale id to reach this one.
    auto sentinel = svc.Enqueue("{}");
    REQUIRE(WaitForFinalStatus(svc, sentinel) == kDone);
    REQUIRE_FALSE(svc.GetById(cancelled).has_value());

    pool.Stop();
}

TEST_CASE("Cancel racing the pool: every job is either deleted or DONE, never stuck") {
    test_log::ExpectTimingDependentLogs(
        "\"[service] ... PENDING -> RUNNING skipped\" each time a cancel beats a worker's claim");
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};
    constexpr int kNumJobs = 100;

    std::vector<JobId> ids;
    for (int i = 0; i < kNumJobs; i++) ids.push_back(svc.Enqueue("{}"));

    ThreadPool pool(svc, 4);
    pool.Start();

    // Cancel every job while workers are picking them up. The DB decides each race.
    std::vector<bool> cancelled(kNumJobs);
    std::thread canceller([&] {
        for (int i = 0; i < kNumJobs; i++) cancelled[i] = svc.CancelPendingJobById(ids[i]);
    });
    canceller.join();

    for (int i = 0; i < kNumJobs; i++) {
        if (cancelled[i]) {
            REQUIRE_FALSE(svc.GetById(ids[i]).has_value());
        } else {
            REQUIRE(WaitForFinalStatus(svc, ids[i]) == kDone);
        }
    }
    REQUIRE(test_db::CountWithStatus(conn, "RUNNING") == 0);
    REQUIRE(test_db::CountWithStatus(conn, "PENDING") == 0);

    pool.Stop();
    REQUIRE(svc.CacheSize() == 0);
}
