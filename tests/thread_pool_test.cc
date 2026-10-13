#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <string>
#include <thread>

#include "http_server.h"
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
    REQUIRE(svc.Remove(id));
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
