#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

#include "job.h"

class JobRepository {
private:
    std::string connection_string_;

    pqxx::connection& Connection();

public:
    explicit JobRepository(std::string connection_string)
        : connection_string_(std::move(connection_string)) {}

    explicit JobRepository(pqxx::connection& conn) : 
        JobRepository(conn.connection_string()) {}

    void CreateJob(Job const&);
    size_t UpdateJobStatus(JobId const&, Status from_status, Status to_status);
    std::optional<Job> GetById(JobId const&);
    std::vector<Job> GetAllJobs();

    size_t ResetRunningToPending();
    std::vector<Job> GetPendingJobOrderByLastUpdate();
};