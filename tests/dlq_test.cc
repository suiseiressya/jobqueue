// S4 slice 2: dead letter queue (D-019). A job that used up its retries is moved to
// dead_letter_jobs (the row stays in jobs as FAILED); POST /dlq/:id/retry takes it out and
// the scheduler runs it again.
#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <thread>

#include "http_server.h"
#include "job_repository.h"
#include "job_service.h"
#include "scheduler.h"
#include "test_db_helpers.h"
#include "thread_pool.h"

using namespace std::chrono_literals;
using Json = nlohmann::json;

namespace {

// Poll until pred() is true or the timeout passes; returns the last result.
bool WaitUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return pred();
}

// A job already in the DLQ: inserted RUNNING, then moved, as a worker would after its last
// retry. `payload` decides whether it can succeed when retried.
JobId InsertDeadJob(pqxx::connection& conn, const std::string& payload) {
    auto id = test_db::InsertJob(conn, payload, "RUNNING", "1 minute");
    test_db::SetRetryState(conn, id, /*retry_count=*/3, /*max_retries=*/3);
    JobRepository repo{conn};
    REQUIRE(repo.Fail(id, "boom"));
    return id;
}

// Same wiring as main.cc: HTTP, pool and scheduler share one JobService.
class DlqFixture {
public:
    pqxx::connection conn_{test_db::kConn};
    JobRepository repo_{conn_};
    JobService svc_{repo_};
    HttpServer server_{svc_};
    ThreadPool pool_{svc_, 2};
    Scheduler scheduler_{svc_, 50ms};
    std::thread server_thread_;
    httplib::Client client_{"localhost", 9092};

    DlqFixture() {
        server_thread_ = std::thread([this] { server_.Listen("127.0.0.1", 9092); });
        client_.set_connection_timeout(2);
        while (!client_.Get("/jobs")) std::this_thread::sleep_for(10ms);
        pool_.Start();
        scheduler_.Start();
    }

    ~DlqFixture() {
        scheduler_.Stop();
        pool_.Stop();
        client_.stop();
        server_.Stop();
        if (server_thread_.joinable()) server_thread_.join();
    }
};

}  // namespace

// --- Repository ---

TEST_CASE("Fail marks a RUNNING job FAILED and adds it to the DLQ in one step") {
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, "{}", "RUNNING", "1 minute");
    JobRepository repo{conn};

    REQUIRE(repo.Fail(id, "boom"));

    auto job = repo.GetById(id);
    REQUIRE(job->job_status == kFailed);
    REQUIRE(job->error_message == "boom");
    REQUIRE(test_db::HasTimestamp(conn, id, "completed_at"));
    REQUIRE(test_db::InDlq(conn, id));
}

TEST_CASE("Fail on a row that is no longer RUNNING writes nothing to either table") {
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    JobRepository repo{conn};

    REQUIRE_FALSE(repo.Fail(id, "boom"));  // D-006

    REQUIRE(test_db::StatusOf(conn, id) == "PENDING");
    REQUIRE(repo.GetById(id)->error_message.empty());
    REQUIRE_FALSE(test_db::InDlq(conn, id));
}

TEST_CASE("RetryFromDlq takes the job out of the DLQ and makes it PENDING and due now") {
    pqxx::connection conn{test_db::kConn};
    auto id = InsertDeadJob(conn, "{}");
    JobRepository repo{conn};

    REQUIRE(repo.RetryFromDlq(id));

    REQUIRE_FALSE(test_db::InDlq(conn, id));
    auto job = repo.GetById(id);
    REQUIRE(job->job_status == kPending);
    REQUIRE(job->retry_count == 0);           // a fresh budget
    REQUIRE(job->error_message.empty());      // cleared: a fresh start
    REQUIRE(test_db::SecondsUntilDue(conn, id) <= 0);
    REQUIRE_FALSE(test_db::HasTimestamp(conn, id, "started_at"));
    REQUIRE_FALSE(test_db::HasTimestamp(conn, id, "completed_at"));

    REQUIRE_FALSE(repo.RetryFromDlq(id));  // no longer in the DLQ
}

TEST_CASE("RetryFromDlq refuses jobs that are not in the DLQ") {
    pqxx::connection conn{test_db::kConn};
    auto pending = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    auto failed = test_db::InsertJob(conn, "{}", "FAILED", "1 minute");  // FAILED, but no DLQ row
    JobRepository repo{conn};

    REQUIRE_FALSE(repo.RetryFromDlq(pending));
    REQUIRE_FALSE(repo.RetryFromDlq(failed));
    REQUIRE_FALSE(repo.RetryFromDlq(JobId::Generate()));
    REQUIRE(test_db::StatusOf(conn, pending) == "PENDING");
    REQUIRE(test_db::StatusOf(conn, failed) == "FAILED");
}

TEST_CASE("GetDlqJobs lists only dead-lettered jobs, oldest move first") {
    pqxx::connection conn{test_db::kConn};
    auto first = InsertDeadJob(conn, "{}");
    auto second = InsertDeadJob(conn, "{}");
    test_db::InsertJob(conn, "{}", "FAILED", "1 minute");  // FAILED, not in the DLQ
    test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    JobRepository repo{conn};

    auto dead = repo.GetDlqJobs();

    REQUIRE(dead.size() == 2);
    REQUIRE(dead[0].id == first);
    REQUIRE(dead[1].id == second);
}

// --- HTTP + pool + scheduler: the real path ---

TEST_CASE("GET /dlq lists dead jobs with their error and retry count") {
    DlqFixture f;
    auto res = f.client_.Get("/dlq");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    REQUIRE(Json::parse(res->body).empty());

    auto id = InsertDeadJob(f.conn_, R"({"fail":true})");

    res = f.client_.Get("/dlq");
    REQUIRE(res);
    auto dead = Json::parse(res->body);
    REQUIRE(dead.size() == 1);
    REQUIRE(dead[0]["id"] == id.ToString());
    REQUIRE(dead[0]["error_message"] == "boom");
    REQUIRE(dead[0]["retry_count"] == 3);
}

TEST_CASE("POST /dlq/:id/retry returns 404 for an unknown job and 409 for one not in the DLQ") {
    DlqFixture f;
    auto pending = test_db::InsertJob(f.conn_, R"({"sleep_ms":10})", "PENDING", "1 minute");
    test_db::ScheduleIn(f.conn_, pending, 60);  // keep it PENDING: the scheduler must not run it

    auto res = f.client_.Post("/dlq/" + JobId::Generate().ToString() + "/retry");
    REQUIRE(res);
    REQUIRE(res->status == 404);

    res = f.client_.Post("/dlq/" + pending.ToString() + "/retry");
    REQUIRE(res);
    REQUIRE(res->status == 409);
}

TEST_CASE("A dead job retried through POST /dlq/:id/retry runs again and ends DONE") {
    DlqFixture f;
    auto id = InsertDeadJob(f.conn_, R"({"sleep_ms":10})");  // succeeds once retried

    auto res = f.client_.Post("/dlq/" + id.ToString() + "/retry");
    REQUIRE(res);
    REQUIRE(res->status == 202);  // queued for the scheduler, not run yet
    REQUIRE_FALSE(test_db::InDlq(f.conn_, id));

    bool done = WaitUntil([&] { return test_db::StatusOf(f.conn_, id) == "DONE"; });
    REQUIRE(done);
}
