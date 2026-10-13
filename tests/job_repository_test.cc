#include "job_repository.h"

#include <catch2/catch_test_macros.hpp>

#include "test_db_helpers.h"

TEST_CASE("ResetRunningToPending resets only RUNNING rows and returns the count") {
    pqxx::connection conn{test_db::kConn};
    auto r1 = test_db::InsertJob(conn, "r1", "RUNNING", "1 minute");
    auto r2 = test_db::InsertJob(conn, "r2", "RUNNING", "1 minute");
    auto pending = test_db::InsertJob(conn, "p", "PENDING", "1 minute");
    auto done = test_db::InsertJob(conn, "d", "DONE", "1 minute");
    auto failed = test_db::InsertJob(conn, "f", "FAILED", "1 minute");

    JobRepository repo{conn};
    REQUIRE(repo.ResetRunningToPending() == 2);

    REQUIRE(test_db::StatusOf(conn, r1) == "PENDING");
    REQUIRE(test_db::StatusOf(conn, r2) == "PENDING");
    REQUIRE(test_db::StatusOf(conn, pending) == "PENDING");
    REQUIRE(test_db::StatusOf(conn, done) == "DONE");
    REQUIRE(test_db::StatusOf(conn, failed) == "FAILED");

    // Idempotent: nothing left to reset.
    REQUIRE(repo.ResetRunningToPending() == 0);
}

TEST_CASE("GetPendingJobOrderByCreatedAt returns only PENDING, oldest first") {
    pqxx::connection conn{test_db::kConn};
    // Inserted out of age order on purpose.
    auto middle = test_db::InsertJob(conn, "middle", "PENDING", "2 minutes");
    auto newest = test_db::InsertJob(conn, "newest", "PENDING", "1 minute");
    auto oldest = test_db::InsertJob(conn, "oldest", "PENDING", "3 minutes");
    test_db::InsertJob(conn, "running", "RUNNING", "10 minutes");
    test_db::InsertJob(conn, "done", "DONE", "10 minutes");

    JobRepository repo{conn};
    auto jobs = repo.GetPendingJobOrderByCreatedAt();

    REQUIRE(jobs.size() == 3);
    REQUIRE(jobs[0].id == oldest);
    REQUIRE(jobs[1].id == middle);
    REQUIRE(jobs[2].id == newest);
    REQUIRE(jobs[0].payload == "oldest");
    REQUIRE(jobs[0].job_status == kPending);
}

TEST_CASE("GetPendingJobOrderByCreatedAt on an empty table returns nothing") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    REQUIRE(repo.GetPendingJobOrderByCreatedAt().empty());
}

// --- CancelPendingJobById: DELETE only while PENDING (I-003) ---

TEST_CASE("CancelPendingJobById delete only PENDING rows") {
    pqxx::connection conn{test_db::kConn};
    auto running = test_db::InsertJob(conn, "r", "RUNNING", "1 minute");
    auto done = test_db::InsertJob(conn, "d", "DONE", "1 minute");
    auto failed = test_db::InsertJob(conn, "f", "FAILED", "1 minute");
    auto id = test_db::InsertJob(conn, "p", "PENDING", "1 minute");

    JobRepository repo{conn};
    REQUIRE(repo.CancelPendingJobById(running) == 0);
    REQUIRE(repo.CancelPendingJobById(done) == 0);
    REQUIRE(repo.CancelPendingJobById(failed) == 0);
    REQUIRE(repo.CancelPendingJobById(id) == 1);
    REQUIRE_FALSE(repo.GetById(id).has_value());

    REQUIRE(test_db::StatusOf(conn, running) == "RUNNING");
    REQUIRE(test_db::StatusOf(conn, done) == "DONE");
    REQUIRE(test_db::StatusOf(conn, failed) == "FAILED");

    // Second cancel finds nothing.
    REQUIRE(repo.CancelPendingJobById(id) == 0);
}

TEST_CASE("CancelPendingJobById returns 0 for an unknown id") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    REQUIRE(repo.CancelPendingJobById(JobId::Generate()) == 0);
}
