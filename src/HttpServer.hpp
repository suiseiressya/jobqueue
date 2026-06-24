#pragma once
#include <httplib.h>

#include "JobService.hpp"

/**
Setup a HTTP server with APIs for the job queue. 
*/
class HttpServer {
private:
    /// Controller 
    httplib::Server _server;

    /// Service
    JobService& _jobService;

    void setupRoutes();

public:
    explicit HttpServer(JobService& jobService);
    void listen(const std::string& host, int port);
    void stop();
};
