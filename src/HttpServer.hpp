#pragma once
#include <httplib.h>

#include "JobService.hpp"

class HttpServer {
private:
    httplib::Server _server;
    JobService& _jobService;

    void setupRoutes();

public:
    explicit HttpServer(JobService& jobService);
    void listen(const std::string& host, int port);
    void stop();
};
