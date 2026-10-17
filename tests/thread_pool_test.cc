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

// A row with no retry budget, loaded through EnqueueDueJobs so the cached job carries it:
// one failure ends it FAILED. Retries are covered in retry_test.cc.
static JobId EnqueueWithoutRetries(pqxx::connection& conn, JobService& svc, const std::string& payload) {
    auto id = test_db::InsertJob(conn, payload, "PENDING", "1 minute");
    test_db::SetRetryState(conn, id, /*retry_count=*/0, /*max_retries=*/0);
    svc.EnqueueDueJobs();
    return id;
}

TEST_CASE("Execute marks job FAILED when payload requests failure") {
    test_log::ExpectFailureLogs(1);
    pqxx::connection conn{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 2);
    pool.Start();

    auto id = EnqueueWithoutRetries(conn, svc, R"({"fail":true})");
    Status status = WaitForFinalStatus(svc, id);

    pool.Stop();  // before REQUIRE: a failed REQUIRE with workers running aborts the run
    REQUIRE(status == kFailed);
}

// POST refuses invalid payloads with 400. Rows written some other way (SQL, rows older than
// that check) can still reach a worker: it must fail them like any error, not crash.
TEST_CASE("A bad-payload row that bypassed POST ends FAILED and the worker keeps running") {
    test_log::ExpectFailureLogs(3);
    pqxx::connection conn{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository repo{conn};
    JobService svc{repo};
    // One worker, so the second job proves the same worker survived the first.
    ThreadPool pool(svc, 1);
    pool.Start();

    auto bad_json = EnqueueWithoutRetries(conn, svc, "not json");
    auto not_object = EnqueueWithoutRetries(conn, svc, "[1, 2]");
    auto bad_type = EnqueueWithoutRetries(conn, svc, R"({"sleep_ms":"ten"})");
    auto good = svc.Enqueue("{}");

    Status bad_json_status = WaitForFinalStatus(svc, bad_json);
    Status not_object_status = WaitForFinalStatus(svc, not_object);
    Status bad_type_status = WaitForFinalStatus(svc, bad_type);
    Status good_status = WaitForFinalStatus(svc, good);

    pool.Stop();  // before REQUIRE: a failed REQUIRE with workers running aborts the run
    REQUIRE(bad_json_status == kFailed);
    REQUIRE(not_object_status == kFailed);
    REQUIRE(bad_type_status == kFailed);
    REQUIRE(good_status == kDone);
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

// I-007. Without a catch around WaitAndPop in WorkerLoop, the claim's exception reaches
// std::terminate: Catch2 reports SIGABRT and the rest of the run is skipped.
TEST_CASE("A worker survives a claim that throws and keeps processing jobs") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 1);
    pool.Start();

    auto warm = svc.Enqueue("{}");  // the worker thread opens its connection
    auto warm_status = WaitForFinalStatus(svc, warm);

    test_log::ExpectFailureLogs(1);  // the failed claim
    test_db::KillOtherConnections(conn);
    REQUIRE_THROWS(svc.GetById(warm));  // this thread's connection died too; now reconnected

    auto stranded = svc.Enqueue("{}");  // the worker's claim of this one throws
    auto next = svc.Enqueue("{}");      // the same worker must reconnect and run this one
    auto next_status = WaitForFinalStatus(svc, next);
    // Stop before the REQUIREs: a failed REQUIRE with workers running aborts the run.
    pool.Stop();

    REQUIRE(warm_status == kDone);
    REQUIRE(next_status == kDone);
    // Not claimed: still PENDING, and nothing re-queues it until the S4 scheduler.
    REQUIRE(test_db::StatusOf(conn, stranded) == "PENDING");
    REQUIRE(svc.CacheSize() == 0);
}
