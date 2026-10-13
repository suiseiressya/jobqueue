#pragma once
#include <pqxx/pqxx>
#include <string>

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

}  // namespace test_db
