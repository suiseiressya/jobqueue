#include "http_server.h"

#include "payload.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <nlohmann/json.hpp>
#include <syncstream>

using Json = nlohmann::json;
using Request = httplib::Request;
using Response = httplib::Response;

static Json JobToJson(const Job& job) {
    return {
        {"id", job.id.ToString()},
        {"payload", job.payload},
        {"status", job.job_status},
        {"retry_count", job.retry_count},
        {"max_retries", job.max_retries},
        {"error_message", job.error_message},
    };
}

/// Every error response has the same shape: {"error": "<message>"}.
static void SendError(Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(Json{{"error", message}}.dump(), "application/json");
}

/// Parse the :id path parameter; a malformed id is the client's mistake, so 400, not 500.
static std::optional<JobId> ParseJobId(const Request& req, Response& res) {
    try {
        return JobId(req.path_params.at("id"));
    } catch (const std::invalid_argument& e) {
        SendError(res, 400, e.what());
        return std::nullopt;
    }
}

/// True if the Content-Type media type is application/json (any case, parameters ignored).
static bool IsJsonContentType(const Request& req) {
    auto type = req.get_header_value("Content-Type");
    type = type.substr(0, type.find(';'));
    std::erase_if(type, [](unsigned char c) { return std::isspace(c); });
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return type == "application/json";
}

HttpServer::HttpServer(JobService& job_service, std::optional<RateLimitConfig> rate_limit)
    : job_service_(job_service) {
    if (rate_limit) rate_limiter_.emplace(*rate_limit);
    SetupRoutes();
}

/**
Route setup. All routes go here.
*/
void HttpServer::SetupRoutes() {
    /**
    Runs before routing, so a refused request is answered before its body is read or parsed.
    1. Rate limit (D-024): one token per request, keyed by client IP.
       @return 429 with Retry-After (whole seconds) when the client's bucket is empty
    2. Body framing: a POST needs Content-Length. httplib checks kMaxBodyBytes against
       Content-Length only; a chunked body would be buffered with no limit, and a body with
       neither header is read until the client closes the connection.
       @return 411 for a POST without Content-Length
    */
    server_.set_pre_routing_handler([this](const Request& req, Response& res) {
        if (rate_limiter_) {
            auto decision = rate_limiter_->Take(req.remote_addr);
            if (!decision.allowed) {
                auto seconds = std::max<long long>(
                    1, std::chrono::ceil<std::chrono::seconds>(decision.retry_after).count());
                res.set_header("Retry-After", std::to_string(seconds));
                SendError(res, 429, "rate limit exceeded");
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        if (req.method == "POST" && !req.has_header("Content-Length")) {
            SendError(res, 411, "Content-Length required");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    /// 413 for a body over kMaxBodyBytes; httplib skips the body without buffering it.
    server_.set_payload_max_length(kMaxBodyBytes);

    /**
    Errors httplib produces itself (unknown route 404, 413, unreadable body 400) have no body;
    give them the same JSON shape as ours. Responses that already have a body are left alone.
    */
    server_.set_error_handler([](const Request& req, Response& res) {
        if (!res.body.empty()) return httplib::Server::HandlerResponse::Unhandled;
        SendError(res, res.status, httplib::status_message(res.status));
        return httplib::Server::HandlerResponse::Handled;
    });

    /**
    An exception escaping a route (e.g. the DB is down) is logged here and answered with a
    generic 500. Without this handler httplib copies e.what() into an EXCEPTION_WHAT header,
    which can carry connection details to the client.
    */
    server_.set_exception_handler([](const Request& req, Response& res, std::exception_ptr ep) {
        std::string what = "unknown exception";
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        std::osyncstream(std::cerr)
            << "HTTP " << req.method << " " << req.path << " failed: " << what << std::endl;
        SendError(res, 500, "internal server error");
    });

    /**
    POST /jobs, requestbody: {"payload": <job payload>}
    Create a new job with payload and enqueue it.
    @return a JSON object of the created job
    */
    server_.Post("/jobs", [this](const Request& req, Response& res) {
        if (!IsJsonContentType(req)) {
            SendError(res, 415, "Content-Type must be application/json");
            return;
        }

        auto body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("payload")) {
            SendError(res, 400, "missing 'payload' field");
            return;
        }

        if (!body["payload"].is_string()) {
            SendError(res, 400, "'payload' must be a JSON string");
            return;
        }
        std::string payload = body["payload"].get<std::string>();

        // Refuse a payload no worker could run: retrying would never fix it.
        try {
            ParsePayload(payload);
        } catch (const std::invalid_argument& e) {
            SendError(res, 400, e.what());
            return;
        }

        auto id = job_service_.Enqueue(payload);
        auto job = job_service_.GetById(id);

        res.status = 201;
        res.set_content(JobToJson(*job).dump(), "application/json");
    });

    /**
    GET /jobs
    Get all jobs.
    @return a JSON object containing all jobs
    */
    server_.Get("/jobs", [this](const Request& req, Response& res) {
        auto jobs = job_service_.GetAll();
        auto arr = Json::array();
        for (const auto& job : jobs) {
            arr.push_back(JobToJson(job));
        }
        res.set_content(arr.dump(), "application/json");
    });

    /**
    GET /jobs/:id
    Get a job with its id.
    @return a JSON object with the job found, or 404 if not found
    */
    server_.Get("/jobs/:id", [this](const Request& req, Response& res) {
        auto id = ParseJobId(req, res);
        if (!id) return;
        auto job = job_service_.GetById(*id);
        if (!job) {
            SendError(res, 404, "job not found");
            return;
        }
        res.set_content(JobToJson(*job).dump(), "application/json");
    });

    /**
    DELETE /jobs/:id
    Delete a job by its id.
    @return 204 if success, 404 if fail
    */
    server_.Delete("/jobs/:id", [this](const Request& req, Response& res) {
        auto id = ParseJobId(req, res);
        if (!id) return;
        auto cancelled = job_service_.CancelPendingJobById(*id);
        if (!cancelled) {
            auto job = job_service_.GetById(*id);
            if (!job) {
                SendError(res, 404, "job not found");
            }
            else {
                SendError(res, 409, "job is not PENDING");
            }
            return;
        }
        res.status = 204;
    });

    /**
    GET /dlq
    Get all jobs in the dead letter queue, oldest move first.
    @return a JSON array of the dead jobs
    */
    server_.Get("/dlq", [this](const Request& req, Response& res) {
        auto arr = Json::array();
        for (const auto& job : job_service_.GetDlqJobs()) {
            arr.push_back(JobToJson(job));
        }
        res.set_content(arr.dump(), "application/json");
    });

    /**
    POST /dlq/:id/retry
    Take a job out of the dead letter queue and make it PENDING; the scheduler queues it.
    @return 202 if queued, 404 if the job does not exist, 409 if it is not in the DLQ
    */
    server_.Post("/dlq/:id/retry", [this](const Request& req, Response& res) {
        auto id = ParseJobId(req, res);
        if (!id) return;
        if (!job_service_.RetryFromDlq(*id)) {
            if (!job_service_.GetById(*id)) {
                SendError(res, 404, "job not found");
            }
            else {
                SendError(res, 409, "job is not in the DLQ");
            }
            return;
        }
        res.status = 202;
    });
}

bool HttpServer::Listen(const std::string& host, int port) {
    return server_.listen(host, port);
}

void HttpServer::Stop() {
    server_.stop();
}

bool HttpServer::IsRunning() {
    return server_.is_running();
}
