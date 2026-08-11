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

}  // namespace

TEST_CASE("POST 1000 jobs are all picked up and completed by the worker pool") {
    ServerFixture f;
    constexpr int kNumJobs = 1000;

    std::set<std::string> ids;
    for (int i = 0; i < kNumJobs; i++) {
        auto res = f.client_.Post("/jobs", R"({"payload":"job-)" + std::to_string(i) + R"("})",
                                   "application/json");
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

TEST_CASE("Removing a job before it is picked up keeps it deleted") {
    pqxx::connection conn_{
        "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService svc{job_repo_};

    // Enqueue and remove before any worker exists, so the id is guaranteed
    // to still be sitting in the queue (never popped) when Start() runs.
    auto id = svc.Enqueue("to-be-deleted");
    REQUIRE(svc.Remove(id));
    REQUIRE_FALSE(svc.GetById(id).has_value());

    ThreadPool pool(svc, 4);
    pool.Start();

    // If the worker pool resurrects the deleted job (see job_service_test),
    // it would appear here shortly after the pool drains the queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    REQUIRE_FALSE(svc.GetById(id).has_value());

    pool.Stop();
}
