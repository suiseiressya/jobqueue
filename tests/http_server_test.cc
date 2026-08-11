#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <thread>

#include "http_server.h"

using Json = nlohmann::json;

class TestFixture {
public:
    pqxx::connection conn_{"host=localhost port=5433 dbname=test user=jobqueue password=jobqueue"};
    JobRepository job_repo_{conn_};
    JobService job_service_{job_repo_};
    HttpServer server_{job_service_};
    std::thread server_thread_;
    httplib::Client client_{"localhost", 9090};

    TestFixture() {
        server_thread_ = std::thread([this] { server_.Listen("127.0.0.1", 9090); });
        client_.set_connection_timeout(2);
        while (!client_.Get("/jobs")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    ~TestFixture() {
        client_.stop();
        server_.Stop();
        if (server_thread_.joinable()) server_thread_.join();
    }
};

TEST_CASE("POST /jobs creates a job") {
    TestFixture f;

    auto res = f.client_.Post("/jobs", R"({"payload":"test-job"})", "application/json");

    REQUIRE(res);
    REQUIRE(res->status == 201);

    auto body = Json::parse(res->body);
    REQUIRE(body["payload"] == "test-job");
    REQUIRE(body["status"] == 0);
    REQUIRE(body["retry_count"] == 0);
    REQUIRE(body.contains("id"));
}

TEST_CASE("POST /jobs returns 400 for missing payload") {
    TestFixture f;

    SECTION("empty body") {
        auto res = f.client_.Post("/jobs", "", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }

    SECTION("no payload field") {
        auto res = f.client_.Post("/jobs", R"({"foo":"bar"})", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }

    SECTION("invalid json") {
        auto res = f.client_.Post("/jobs", "not json", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }
}

TEST_CASE("GET /jobs returns all jobs") {
    TestFixture f;

    auto res = f.client_.Get("/jobs");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    REQUIRE(Json::parse(res->body).size() == 0);

    f.client_.Post("/jobs", R"({"payload":"a"})", "application/json");
    f.client_.Post("/jobs", R"({"payload":"b"})", "application/json");

    res = f.client_.Get("/jobs");
    REQUIRE(res);
    auto jobs = Json::parse(res->body);
    REQUIRE(jobs.size() == 2);
}

TEST_CASE("GET /jobs/:id returns single job") {
    TestFixture f;

    auto create_res = f.client_.Post("/jobs", R"({"payload":"find-me"})", "application/json");
    auto id = Json::parse(create_res->body)["id"].get<std::string>();

    auto res = f.client_.Get("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 200);

    auto body = Json::parse(res->body);
    REQUIRE(body["id"] == id);
    REQUIRE(body["payload"] == "find-me");
}

TEST_CASE("GET /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client_.Get("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}

TEST_CASE("DELETE /jobs/:id removes job") {
    TestFixture f;

    auto create_res = f.client_.Post("/jobs", R"({"payload":"delete-me"})", "application/json");
    auto id = Json::parse(create_res->body)["id"].get<std::string>();

    auto res = f.client_.Delete("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 204);

    auto get_res = f.client_.Get("/jobs/" + id);
    REQUIRE(get_res);
    REQUIRE(get_res->status == 404);
}

TEST_CASE("DELETE /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client_.Delete("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}
