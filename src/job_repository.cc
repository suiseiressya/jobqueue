#include "job_repository.h"
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

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
    pqxx::work txn(conn_);
    txn.exec_params(
        "INSERT INTO jobs (id, payload, job_status, retry_count) "
        "VALUES ($1, $2::jsonb, $3, $4)",

        job.id.ToString(),
        nlohmann::json(job.payload).dump(),
        StatusToString(job.job_status),
        job.retry_count
    );
    txn.commit();
}

/**
Update status of a Job based on job_id and from_status. 
Need from_status to prevent stale updating (Job changed status before transaction)
*/
size_t JobRepository::UpdateJobStatus(JobId const& id, Status from_status, Status to_status) {
    pqxx::work txn(conn_);

    pqxx::result result = txn.exec_params(
        "UPDATE jobs "
        "SET job_status = $1 "
        "WHERE id = $2 AND job_status = $3", 

        StatusToString(to_status), 
        id.ToString(), 
        StatusToString(from_status)
    );
    txn.commit();

    return result.affected_rows();
}

std::optional<Job> JobRepository::GetById(JobId const& id) {
    pqxx::work txn(conn_);

    pqxx::result result = txn.exec_params(
        "SELECT id, payload, job_status, retry_count FROM jobs "
        "WHERE id = $1", 
        id.ToString()
    );

    if (result.empty()) return std::nullopt;

    auto row = result[0];
    Job job;
    job.id = JobId(row["id"].as<std::string>());
    job.payload = row["payload"].c_str();
    job.job_status = StringToStatus(row["job_status"].c_str());
    job.retry_count = row["retry_count"].as<int>();

    return job;
}

std::vector<Job> JobRepository::GetAllJobs() {
    pqxx::work txn(conn_);

    pqxx::result result = txn.exec(
        "SELECT id, payload #>> '{}' AS payload, job_status, retry_count "
        "FROM jobs ORDER BY created_at ASC"
    );

    std::vector<Job> jobs;
    for (auto const& row : result) {
        Job job;
        job.id = JobId(row["id"].as<std::string>());
        job.payload = row["payload"].c_str();
        job.job_status = StringToStatus(row["job_status"].c_str());
        job.retry_count = row["retry_count"].as<int>();
        jobs.push_back(job);
    }

    return jobs;
}
