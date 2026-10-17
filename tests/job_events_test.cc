// S5 slice 1: metrics come from the DB, not the app (D-023). Triggers from migration 005 count
// every job transition in job_event_counts, inside the transaction that makes it, so a count can
// never lead or outlive the state change it reports. sql_exporter turns the table into
// Prometheus metrics; these tests check the counting itself through the real repository calls.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "job.h"
#include "job_repository.h"
#include "test_db_helpers.h"

using namespace std::chrono_literals;

namespace {

long EventCount(pqxx::connection& conn, const std::string& event) {
    pqxx::work txn(conn);
    auto result = txn.exec_params("SELECT n FROM job_event_counts WHERE event = $1", event);
    return result.empty() ? 0 : result[0][0].as<long>();
}

}  // namespace

TEST_CASE("Each repository transition is counted once under its event name") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};

    Job job(JobId::Generate(), "{}");
    repo.CreateJob(job);
    REQUIRE(EventCount(conn, "enqueued") == 1);

    REQUIRE(repo.UpdateJobStatus(job.id, kPending, kRunning) == 1);
    REQUIRE(EventCount(conn, "claimed") == 1);

    REQUIRE(repo.Retry(job.id, 0ms, "boom"));
    REQUIRE(EventCount(conn, "retried") == 1);

    REQUIRE(repo.UpdateJobStatus(job.id, kPending, kRunning) == 1);
    REQUIRE(repo.Fail(job.id, "boom"));
    REQUIRE(EventCount(conn, "failed") == 1);
    REQUIRE(EventCount(conn, "dlq") == 1);

    REQUIRE(repo.RetryFromDlq(job.id));
    REQUIRE(EventCount(conn, "dlq_retried") == 1);

    REQUIRE(repo.UpdateJobStatus(job.id, kPending, kRunning) == 1);
    REQUIRE(repo.UpdateJobStatus(job.id, kRunning, kDone) == 1);
    REQUIRE(EventCount(conn, "completed") == 1);
    REQUIRE(EventCount(conn, "claimed") == 3);
}

TEST_CASE("Startup recovery and cancel are counted apart from retries") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    test_db::InsertJob(conn, "{}", "RUNNING", "1 minute");
    auto pending = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");

    REQUIRE(repo.ResetRunningToPending() == 1);
    REQUIRE(EventCount(conn, "recovered") == 1);
    REQUIRE(EventCount(conn, "retried") == 0);  // retry_count did not change

    REQUIRE(repo.CancelPendingJobById(pending));
    REQUIRE(EventCount(conn, "cancelled") == 1);
}

TEST_CASE("A transition that writes 0 rows or rolls back counts nothing") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    auto pending = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    REQUIRE(EventCount(conn, "enqueued") == 1);

    REQUIRE(repo.UpdateJobStatus(pending, kRunning, kDone) == 0);  // D-006
    REQUIRE_FALSE(repo.Fail(pending, "boom"));
    REQUIRE(EventCount(conn, "completed") == 0);
    REQUIRE(EventCount(conn, "failed") == 0);
    REQUIRE(EventCount(conn, "dlq") == 0);

    {
        pqxx::work txn(conn);
        txn.exec_params("UPDATE jobs SET job_status = 'RUNNING' WHERE id = $1", pending.ToString());
        // destroyed without commit: rolled back
    }
    REQUIRE(EventCount(conn, "claimed") == 0);
    REQUIRE(test_db::StatusOf(conn, pending) == "PENDING");
}

TEST_CASE("An UPDATE that keeps job_status counts nothing") {
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");

    test_db::ScheduleIn(conn, id, 5);  // touches the row, not its status
    test_db::SetRetryState(conn, id, 1, 3);

    pqxx::work txn(conn);
    REQUIRE(txn.exec1("SELECT sum(n) FROM job_event_counts")[0].as<long>() == 1);  // the insert
}
