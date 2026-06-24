#include "HttpServer.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Request = httplib::Request;
using Response = httplib::Response;

static json jobToJson(const Job& job) {
    return {
        {"id", job.id.to_string()},
        {"payload", job.payload},
        {"status", job.status},
        {"retry_count", job.retry_count},
    };
}

HttpServer::HttpServer(JobService& jobService) : _jobService(jobService) {
    setupRoutes();
}

/**
Route setup. All routes go here. 
*/
void HttpServer::setupRoutes() {
    /**
    POST /jobs, requestbody: {"payload": <job payload>}
    Create a new job with payload and enqueue it.
    @return a JSON object of the created job
    */
    _server.Post("/jobs", [this](const Request& req, Response& res) {
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("payload")) {
            res.status = 400;
            res.set_content(json{{"error", "missing 'payload' field"}}.dump(), "application/json");
            return;
        }

        auto id = _jobService.enqueue(body["payload"].get<std::string>());
        auto job = _jobService.get(id);

        res.status = 201;
        res.set_content(jobToJson(*job).dump(), "application/json");
    });

    /**
    GET /jobs 
    Get all jobs.
    @return a JSON object containing all jobs
    */
    _server.Get("/jobs", [this](const Request& req, Response& res) {
        auto jobs = _jobService.getAll();
        auto arr = json::array();
        for (const auto& job : jobs) {
            arr.push_back(jobToJson(job));
        }
        res.set_content(arr.dump(), "application/json");
    });

    /**
    GET /jobs/:id
    Get a job with its id. 
    @return a JSON object with the job found, or 404 if not found
    */
    _server.Get("/jobs/:id", [this](const Request& req, Response& res) {
        auto job = _jobService.get(JobId(req.path_params.at("id")));
        if (!job) {
            res.status = 404;
            res.set_content(json{{"error", "job not found"}}.dump(), "application/json");
            return;
        }
        res.set_content(jobToJson(*job).dump(), "application/json");
    });

    /**
    DELETE /jobs/:id
    Delete a job by its id. 
    @return 204 if success, 404 if fail
    */
    _server.Delete("/jobs/:id", [this](const Request& req, Response& res) {
        auto removed = _jobService.remove(JobId(req.path_params.at("id")));
        if (!removed) {
            res.status = 404;
            res.set_content(json{{"error", "job not found"}}.dump(), "application/json");
            return;
        }
        res.status = 204;
    });
}

void HttpServer::listen(const std::string& host, int port) {
    _server.listen(host, port);
}

void HttpServer::stop() {
    _server.stop();
}
