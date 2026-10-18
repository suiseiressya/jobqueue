#pragma once
#include <httplib.h>

#include <optional>

#include "job_service.h"
#include "rate_limiter.h"

/**
Setup a HTTP server with APIs for the job queue.
*/
class HttpServer {
private:
    /// Controller
    httplib::Server server_;

    /// Service
    JobService& job_service_;

    /// Per-IP limit checked before routing; empty = no limit (D-024)
    std::optional<RateLimiter> rate_limiter_;

    void SetupRoutes();

public:
    /// largest request body accepted
    static constexpr size_t kMaxBodyBytes = 64 * 1024;

    explicit HttpServer(JobService& job_service,
                        std::optional<RateLimitConfig> rate_limit = std::nullopt);
    bool Listen(const std::string& host, int port);
    void Stop();
    bool IsRunning();
};
