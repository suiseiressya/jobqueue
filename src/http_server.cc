#include "http_server.h"

#include <httplib.h>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;
using Request = httplib::Request;
using Response = httplib::Response;

static Json JobToJson(const Job& job) {
    return {
        {"id", job.id.ToString()},
        {"payload", job.payload},
        {"status", job.status},
        {"retry_count", job.retry_count},
    };
}

HttpServer::HttpServer(JobService& job_service) : job_service_(job_service) {
    SetupRoutes();
}

/**
Route setup. All routes go here.
*/
void HttpServer::SetupRoutes() {
    /**
    POST /jobs, requestbody: {"payload": <job payload>}
    Create a new job with payload and enqueue it.
    @return a JSON object of the created job
    */
    server_.Post("/jobs", [this](const Request& req, Response& res) {
        auto body = Json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("payload")) {
            res.status = 400;
            res.set_content(Json{{"error", "missing 'payload' field"}}.dump(), "application/json");
            return;
        }

        auto id = job_service_.Enqueue(body["payload"].get<std::string>());
        auto job = job_service_.Get(id);

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
        auto job = job_service_.Get(JobId(req.path_params.at("id")));
        if (!job) {
            res.status = 404;
            res.set_content(Json{{"error", "job not found"}}.dump(), "application/json");
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
        auto removed = job_service_.Remove(JobId(req.path_params.at("id")));
        if (!removed) {
            res.status = 404;
            res.set_content(Json{{"error", "job not found"}}.dump(), "application/json");
            return;
        }
        res.status = 204;
    });
}

void HttpServer::Listen(const std::string& host, int port) {
    server_.listen(host, port);
}

void HttpServer::Stop() {
    server_.stop();
}
