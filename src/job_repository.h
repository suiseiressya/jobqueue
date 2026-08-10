#include "job.h"
#include <vector>
#include <optional>
#include <pqxx/pqxx>

class JobRepository {
public:
    explicit JobRepository(pqxx::connection& conn) : conn_(conn) {}

    void CreateJob(Job const&);
    size_t UpdateJobStatus(JobId const&, Status from_status, Status to_status);
    std::optional<Job> GetById(JobId const&);
    std::vector<Job> GetAllJobs();

    size_t ResetRunningToPending();
    std::vector<Job> GetPendingJobOrderByLastUpdate();

private:
    pqxx::connection& conn_;
};