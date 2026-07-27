#pragma once
#include <httplib.h>

#include "job_service.h"

/**
Setup a HTTP server with APIs for the job queue.
*/
class HttpServer {
private:
    /// Controller
    httplib::Server server_;

    /// Service
    JobService& job_service_;

    void SetupRoutes();

public:
    explicit HttpServer(JobService& job_service);
    void Listen(const std::string& host, int port);
    void Stop();
};
