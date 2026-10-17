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

    auto res = f.client_.Post("/jobs", R"({"payload":"{\"sleep_ms\":1}"})", "application/json");

    REQUIRE(res);
    REQUIRE(res->status == 201);

    auto body = Json::parse(res->body);
    REQUIRE(body["payload"] == R"({"sleep_ms":1})");
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

// The payload is the fake-work spec (D-015): a JSON object with optional integer
// "sleep_ms" and boolean "fail". Anything else is refused at the door, so it never becomes
// a job; retrying could not fix it.
TEST_CASE("POST /jobs returns 400 for an invalid payload and creates no job") {
    TestFixture f;

    auto expect_rejected = [&](const std::string& body) {
        auto res = f.client_.Post("/jobs", body, "application/json");
        INFO("body " << body);
        REQUIRE(res);
        REQUIRE(res->status == 400);
        REQUIRE(Json::parse(res->body).contains("error"));
    };

    SECTION("payload is not a string") {
        expect_rejected(R"({"payload":5})");
        expect_rejected(R"({"payload":{"sleep_ms":1}})");
    }
    SECTION("payload is not JSON") { expect_rejected(R"({"payload":"not json"})"); }
    SECTION("payload is JSON but not an object") { expect_rejected(R"({"payload":"[1, 2]"})"); }
    SECTION("sleep_ms is not an integer") {
        expect_rejected(R"({"payload":"{\"sleep_ms\":\"ten\"}"})");
        expect_rejected(R"({"payload":"{\"sleep_ms\":1.5}"})");
        expect_rejected(R"({"payload":"{\"sleep_ms\":-5}"})");
    }
    SECTION("fail is not a boolean") { expect_rejected(R"({"payload":"{\"fail\":1}"})"); }

    auto res = f.client_.Get("/jobs");
    REQUIRE(res);
    REQUIRE(Json::parse(res->body).size() == 0);
}

TEST_CASE("POST /jobs accepts every valid fake-work spec") {
    TestFixture f;

    for (const char* body : {R"({"payload":"{}"})", R"({"payload":"{\"sleep_ms\":10}"})",
                             R"({"payload":"{\"fail\":true}"})",
                             R"({"payload":"{\"sleep_ms\":0,\"fail\":false}"})"}) {
        auto res = f.client_.Post("/jobs", body, "application/json");
        INFO("body " << body);
        REQUIRE(res);
        REQUIRE(res->status == 201);
    }
}

TEST_CASE("GET /jobs returns all jobs") {
    TestFixture f;

    auto res = f.client_.Get("/jobs");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    REQUIRE(Json::parse(res->body).size() == 0);

    f.client_.Post("/jobs", R"({"payload":"{}"})", "application/json");
    f.client_.Post("/jobs", R"({"payload":"{}"})", "application/json");

    res = f.client_.Get("/jobs");
    REQUIRE(res);
    auto jobs = Json::parse(res->body);
    REQUIRE(jobs.size() == 2);
}

TEST_CASE("GET /jobs/:id returns single job") {
    TestFixture f;

    auto create_res = f.client_.Post("/jobs", R"({"payload":"{\"sleep_ms\":2}"})", "application/json");
    auto id = Json::parse(create_res->body)["id"].get<std::string>();

    auto res = f.client_.Get("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 200);

    auto body = Json::parse(res->body);
    REQUIRE(body["id"] == id);
    REQUIRE(body["payload"] == R"({"sleep_ms":2})");
}

TEST_CASE("GET /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client_.Get("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}

TEST_CASE("DELETE /jobs/:id deletes a PENDING job") {
    TestFixture f;

    auto create_res = f.client_.Post("/jobs", R"({"payload":"{}"})", "application/json");
    auto id = Json::parse(create_res->body)["id"].get<std::string>();

    auto res = f.client_.Delete("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 204);

    auto get_res = f.client_.Get("/jobs/" + id);
    REQUIRE(get_res);
    REQUIRE(get_res->status == 404);

    // Deleting again: the row is gone.
    auto again = f.client_.Delete("/jobs/" + id);
    REQUIRE(again);
    REQUIRE(again->status == 404);
}

TEST_CASE("DELETE /jobs/:id returns 409 for a job that is not PENDING") {
    TestFixture f;

    auto create_res = f.client_.Post("/jobs", R"({"payload":"{}"})", "application/json");
    auto id = Json::parse(create_res->body)["id"].get<std::string>();
    REQUIRE(f.job_service_.WaitAndPop().has_value());  // no pool in this fixture: claim it by hand

    auto res = f.client_.Delete("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 409);

    auto get_res = f.client_.Get("/jobs/" + id);
    REQUIRE(get_res);
    REQUIRE(Json::parse(get_res->body)["status"] == kRunning);
}

TEST_CASE("DELETE /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client_.Delete("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}
