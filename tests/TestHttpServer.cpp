#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <thread>

#include "HttpServer.hpp"

using json = nlohmann::json;

class TestFixture {
public:
    JobService jobService;
    HttpServer server{jobService};
    std::thread serverThread;
    httplib::Client client{"localhost", 9090};

    TestFixture() {
        serverThread = std::thread([this] { server.listen("127.0.0.1", 9090); });
        client.set_connection_timeout(2);
        while (!client.Get("/jobs")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    ~TestFixture() {
        client.stop();
        server.stop();
        if (serverThread.joinable()) serverThread.join();
    }
};

TEST_CASE("POST /jobs creates a job") {
    TestFixture f;

    auto res = f.client.Post("/jobs", R"({"payload":"test-job"})", "application/json");

    REQUIRE(res);
    REQUIRE(res->status == 201);

    auto body = json::parse(res->body);
    REQUIRE(body["payload"] == "test-job");
    REQUIRE(body["status"] == 0);
    REQUIRE(body["retry_count"] == 0);
    REQUIRE(body.contains("id"));
}

TEST_CASE("POST /jobs returns 400 for missing payload") {
    TestFixture f;

    SECTION("empty body") {
        auto res = f.client.Post("/jobs", "", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }

    SECTION("no payload field") {
        auto res = f.client.Post("/jobs", R"({"foo":"bar"})", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }

    SECTION("invalid json") {
        auto res = f.client.Post("/jobs", "not json", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
    }
}

TEST_CASE("GET /jobs returns all jobs") {
    TestFixture f;

    auto res = f.client.Get("/jobs");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    REQUIRE(json::parse(res->body).size() == 0);

    f.client.Post("/jobs", R"({"payload":"a"})", "application/json");
    f.client.Post("/jobs", R"({"payload":"b"})", "application/json");

    res = f.client.Get("/jobs");
    REQUIRE(res);
    auto jobs = json::parse(res->body);
    REQUIRE(jobs.size() == 2);
}

TEST_CASE("GET /jobs/:id returns single job") {
    TestFixture f;

    auto createRes = f.client.Post("/jobs", R"({"payload":"find-me"})", "application/json");
    auto id = json::parse(createRes->body)["id"].get<std::string>();

    auto res = f.client.Get("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["id"] == id);
    REQUIRE(body["payload"] == "find-me");
}

TEST_CASE("GET /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client.Get("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}

TEST_CASE("DELETE /jobs/:id removes job") {
    TestFixture f;

    auto createRes = f.client.Post("/jobs", R"({"payload":"delete-me"})", "application/json");
    auto id = json::parse(createRes->body)["id"].get<std::string>();

    auto res = f.client.Delete("/jobs/" + id);
    REQUIRE(res);
    REQUIRE(res->status == 204);

    auto getRes = f.client.Get("/jobs/" + id);
    REQUIRE(getRes);
    REQUIRE(getRes->status == 404);
}

TEST_CASE("DELETE /jobs/:id returns 404 for unknown id") {
    TestFixture f;

    auto res = f.client.Delete("/jobs/00000000-0000-0000-0000-000000000000");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}
