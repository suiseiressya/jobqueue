#include "job_repository.h"
#include <mutex>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

/**
Get connection string for current thread
If not exist, create new, else reuse
If broken (e.g Postgres restart/dead), replace connection 
with new one 
*/
pqxx::connection& JobRepository::Connection() {
    // is_open: checking if connection is currently open 
    // if DB restarts, is_open will be wrong and require a reset()
    if (!connection_ || !connection_->is_open()) {
        // mutex to fix pqxx::connection bug
        // New pqxx::connection is spun up for the first time among multiple threads
        // which can cause a data race without a lock.
        // This mutex protects against that behavior, and the impact is minimal:
        // the mutex will only be used one time when each thread when creating new
        // pqxx::connection, and just reuse directly without mutex later
        std::lock_guard lock(connection_init_mut);

        connection_.reset();
        connection_.emplace(pqxx::connection(connection_string_));
    }
    
    return connection_.value();
}

std::string const StatusToString(Status status) {
    switch (status) {
        case kPending: return "PENDING";
        case kRunning: return "RUNNING";
        case kDone: return "DONE";
        case kFailed: return "FAILED";
    }
    throw std::invalid_argument("unknown status");
}

Status StringToStatus(const char* status_str) {
    if (status_str == nullptr) {
        throw std::invalid_argument("null status string");
    }
    std::string str = status_str;
    if (str == "PENDING") return kPending;
    if (str == "RUNNING") return kRunning;
    if (str == "DONE") return kDone;
    if (str == "FAILED") return kFailed;
    throw std::invalid_argument(std::string("unknown status: ") + str);
}


/**
Persist a Job in the database. 
Job: Job object 
*/
void JobRepository::CreateJob(const Job& job) {
    pqxx::work txn(Connection());
    txn.exec_params(
        "INSERT INTO jobs (id, payload, job_status, retry_count, max_retries) "
        "VALUES ($1, $2::jsonb, $3, $4, $5)",

        job.id.ToString(),
        nlohmann::json(job.payload).dump(),
        StatusToString(job.job_status),
        job.retry_count,
        job.max_retries
    );
    txn.commit();
}

static Job RowToJob(pqxx::row const& row) {
    Job job;
    job.id = JobId(row["id"].as<std::string>());
    job.payload = row["payload"].c_str();
    job.job_status = StringToStatus(row["job_status"].c_str());
    job.retry_count = row["retry_count"].as<int>();
    job.max_retries = row["max_retries"].as<int>();
    job.error_message = row["error_message"].c_str();

    return job;
}

static std::string const SELECT_JOB_COLUMNS =
    "SELECT id, payload #>> '{}' AS payload, job_status, retry_count, max_retries, "
    "COALESCE(error_message, '') AS error_message";

/**
Update status of a Job based on job_id and from_status. 
Need from_status to prevent stale updating (Job changed status before transaction)
Also stamps started_at on RUNNING and completed_at on DONE/FAILED.
*/
size_t JobRepository::UpdateJobStatus(JobId const& id, Status from_status, Status to_status) {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        "UPDATE jobs "
        "SET job_status = $1::text, updated_at = NOW(), "
        "    started_at = CASE WHEN $1::text = 'RUNNING' THEN NOW() ELSE started_at END, "
        "    completed_at = CASE WHEN $1::text IN ('DONE', 'FAILED') THEN NOW() ELSE completed_at END "
        "WHERE id = $2 AND job_status = $3", 

        StatusToString(to_status), 
        id.ToString(), 
        StatusToString(from_status)
    );
    txn.commit();

    return result.affected_rows();
}

/**
Get a Job by its Id. 
*/
std::optional<Job> JobRepository::GetById(JobId const& id) {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        SELECT_JOB_COLUMNS + " FROM jobs WHERE id = $1",
        id.ToString()
    );

    if (result.empty()) return std::nullopt;

    return RowToJob(result[0]);
}

/**
Get all jobs in the db ordered by created_at asc. 
*/
std::vector<Job> JobRepository::GetAllJobs() {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec(
        SELECT_JOB_COLUMNS + " FROM jobs ORDER BY created_at ASC"
    );

    std::vector<Job> jobs;
    for (auto const& row : result) 
        jobs.push_back(RowToJob(row));

    return jobs;
}

/**
 * Restat all stale running job in DB to pending. 
 * @return number of rows affected
 */
size_t JobRepository::ResetRunningToPending() {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec(
        "UPDATE jobs "
        "SET job_status = 'PENDING', updated_at = NOW(), started_at = NULL "
        "WHERE job_status = 'RUNNING'" 
    );
    txn.commit();

    return result.affected_rows();
}

std::vector<Job> JobRepository::GetPendingJobOrderByCreatedAt() {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec(
        SELECT_JOB_COLUMNS + 
        " FROM jobs WHERE job_status = 'PENDING' ORDER BY created_at ASC"
    );

    std::vector<Job> jobs;
    for (auto const& row : result) 
        jobs.push_back(RowToJob(row));

    return jobs;
}

bool JobRepository::CancelPendingJobById(JobId const& id) {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        "DELETE FROM jobs "
        "WHERE id = $1 and job_status = 'PENDING'", 
        id.ToString()
    );
    txn.commit();

    return result.affected_rows();
}

// Retrying a (failed) RUNNING job. FAILED means moving to DLQ. 
// Resets it to PENDING and scheduler will pop it out later. 
bool JobRepository::Retry(JobId const& id, std::chrono::milliseconds delay, std::string const& error) {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        "UPDATE jobs "
        "SET job_status = 'PENDING', retry_count = retry_count + 1, error_message = $2,"
        "updated_at = NOW(), started_at = NULL, scheduled_at = MAKE_INTERVAL(secs => $3) + NOW()"
        "WHERE id = $1 and job_status = 'RUNNING'", 

        id.ToString(), 
        error, 
        delay.count() / 1000.0
    );
    txn.commit();

    return result.affected_rows();
}

// Fail a job and move to DLQ.
bool JobRepository::Fail(JobId const& id, std::string const& error) {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        "UPDATE jobs "
        "SET job_status = 'FAILED', error_message = $2, updated_at = NOW(), completed_at = NOW()" 
        "WHERE id = $1 and job_status = 'RUNNING'", 

        id.ToString(), 
        error
    );
    if (result.affected_rows() == 0) return 0;

    txn.exec_params(
        "INSERT INTO dead_letter_jobs (job_id) VALUES ($1)", 
        id.ToString()
    );
    txn.commit();

    return 1;
}

/**
 * Get pending jobs with scheduled_at <= NOW(), earliest first
 */
std::vector<Job> JobRepository::GetDuePendingJobs() {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec(
        SELECT_JOB_COLUMNS + 
        " FROM jobs WHERE job_status = 'PENDING' AND scheduled_at <= NOW() "
        "ORDER BY scheduled_at ASC"
    );

    std::vector<Job> jobs;
    for (auto const& row : result)
        jobs.push_back(RowToJob(row));

    return jobs;
}

/**
 * Retry a job from DLQ. 
 * Delete the row from dlq and reset the row in jobs
 */
bool JobRepository::RetryFromDlq(JobId const& id) {
    pqxx::work txn(Connection());

    pqxx::result dlq = txn.exec_params(
        "DELETE FROM dead_letter_jobs WHERE job_id = $1",
        id.ToString()
    );

    // nothing happened, then don't commit
    if (dlq.affected_rows() == 0) return 0;
    
    pqxx::result result = txn.exec_params(
        "UPDATE jobs "
        "SET job_status = 'PENDING', retry_count = 0, error_message = NULL, updated_at = NOW(), "
        "completed_at = NULL, scheduled_at = NOW(), started_at = NULL " 
        "WHERE id = $1 and job_status = 'FAILED' ",
        
        id.ToString()
    );
    // not FAILED (should not happen): return without commit, the DLQ row stays
    if (result.affected_rows() == 0) return 0;
    txn.commit();

    return 1;
}

/**
 * Get jobs from DLQ, ordered by moved_at. 
 */
std::vector<Job> JobRepository::GetDlqJobs() {
    pqxx::work txn(Connection());

    pqxx::result result = txn.exec_params(
        SELECT_JOB_COLUMNS + " FROM jobs "
        " JOIN dead_letter_jobs dlq on jobs.id = dlq.job_id "
        " ORDER BY dlq.moved_at ASC"
    );
    txn.commit();

    std::vector<Job> jobs;
    for (auto const& row : result)
        jobs.push_back(RowToJob(row));

    return jobs;
}