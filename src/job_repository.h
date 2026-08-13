#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

#include "job.h"

class JobRepository {
private:
    std::string connection_string_;

    // separate thread_local pqxx::connection for each thread
    // use std::optional<> to delay construction 
    // somehow using this instead of unique_ptr solve helgrind problems? 
    inline static thread_local std::optional<pqxx::connection> connection_;
    std::mutex connection_init_mut;
    pqxx::connection&  Connection();

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