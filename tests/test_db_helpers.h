#pragma once
#include <chrono>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>
#include <thread>

#include "job_id.h"

// Shared helpers for tests that need to set up or inspect rows directly in the `test` DB.
namespace test_db {

inline constexpr const char* kConn =
    "host=localhost port=5433 dbname=test user=jobqueue password=jobqueue";

// Insert a job row bypassing JobService, e.g. to simulate state left by a crashed process.
// `age` is a Postgres interval ("3 minutes"); created_at = now() - age.
inline JobId InsertJob(pqxx::connection& conn, const std::string& payload,
                       const std::string& status, const std::string& age) {
    JobId id = JobId::Generate();
    pqxx::work txn(conn);
    txn.exec_params(
        "INSERT INTO jobs (id, payload, job_status, created_at) "
        "VALUES ($1, to_jsonb($2::text), $3, now() - $4::interval)",
        id.ToString(), payload, status, age);
    txn.commit();
    return id;
}

inline std::string StatusOf(pqxx::connection& conn, const JobId& id) {
    pqxx::work txn(conn);
    auto row = txn.exec_params1("SELECT job_status FROM jobs WHERE id = $1", id.ToString());
    return row[0].as<std::string>();
}

// True if the timestamp column (started_at / completed_at) of the row is set.
inline bool HasTimestamp(pqxx::connection& conn, const JobId& id, const std::string& column) {
    pqxx::work txn(conn);
    auto row = txn.exec_params1("SELECT " + txn.quote_name(column) + " IS NOT NULL FROM jobs WHERE id = $1",
                                id.ToString());
    return row[0].as<bool>();
}

inline int CountWithStatus(pqxx::connection& conn, const std::string& status) {
    pqxx::work txn(conn);
    auto row = txn.exec_params1("SELECT count(*) FROM jobs WHERE job_status = $1", status);
    return row[0].as<int>();
}

// Set the retry columns directly, e.g. a job that has already used part of its budget.
inline void SetRetryState(pqxx::connection& conn, const JobId& id, int retry_count, int max_retries) {
    pqxx::work txn(conn);
    txn.exec_params("UPDATE jobs SET retry_count = $2, max_retries = $3 WHERE id = $1",
                    id.ToString(), retry_count, max_retries);
    txn.commit();
}

// Move scheduled_at to now() + seconds; a negative value makes the row already due.
inline void ScheduleIn(pqxx::connection& conn, const JobId& id, int seconds) {
    pqxx::work txn(conn);
    txn.exec_params("UPDATE jobs SET scheduled_at = now() + make_interval(secs => $2) WHERE id = $1",
                    id.ToString(), seconds);
    txn.commit();
}

// Seconds from now until the row's scheduled_at (negative once it is due).
inline double SecondsUntilDue(pqxx::connection& conn, const JobId& id) {
    pqxx::work txn(conn);
    auto row = txn.exec_params1("SELECT extract(epoch FROM scheduled_at - now()) FROM jobs WHERE id = $1",
                                id.ToString());
    return row[0].as<double>();
}

// True if the job has a row in dead_letter_jobs.
inline bool InDlq(pqxx::connection& conn, const JobId& id) {
    pqxx::work txn(conn);
    auto row = txn.exec_params1("SELECT count(*) FROM dead_letter_jobs WHERE job_id = $1", id.ToString());
    return row[0].as<int>() == 1;
}

// Terminate every other backend connected to this DB and wait until they are gone.
// pg_terminate_backend only sends the signal, so poll pg_stat_activity.
inline void KillOtherConnections(pqxx::connection& conn) {
    pqxx::nontransaction txn(conn);
    txn.exec(
        "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
        "WHERE datname = current_database() AND pid <> pg_backend_pid()");
    for (int i = 0; i < 100; i++) {
        auto row = txn.exec1(
            "SELECT count(*) FROM pg_stat_activity "
            "WHERE datname = current_database() AND pid <> pg_backend_pid()");
        if (row[0].as<int>() == 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("other connections did not terminate");
}

}  // namespace test_db
