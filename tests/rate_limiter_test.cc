// S5 slice 2: per-IP token bucket in front of every route (D-024). A bucket holds up to `burst`
// tokens and refills at `rate_per_sec`; a request takes one token or gets 429 with Retry-After.
// Time comes from steady_clock (wall-clock jumps cannot refill or drain a bucket) and is passed
// in, so these tests move time instead of sleeping.
#include "rate_limiter.h"

#include <httplib.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "http_server.h"
#include "job_repository.h"
#include "job_service.h"
#include "test_db_helpers.h"

using namespace std::chrono_literals;
using Clock = RateLimiter::Clock;

TEST_CASE("A new key gets `burst` requests at once, then is refused with a retry delay") {
    RateLimiter limiter({.rate_per_sec = 1, .burst = 3});
    auto t0 = Clock::now();

    REQUIRE(limiter.Take("a", t0).allowed);
    REQUIRE(limiter.Take("a", t0).allowed);
    REQUIRE(limiter.Take("a", t0).allowed);

    auto refused = limiter.Take("a", t0);
    REQUIRE_FALSE(refused.allowed);
    REQUIRE(refused.retry_after == 1000ms);  // one token at 1/s
}

TEST_CASE("Tokens refill at rate_per_sec and never exceed burst") {
    RateLimiter limiter({.rate_per_sec = 2, .burst = 2});
    auto t0 = Clock::now();
    REQUIRE(limiter.Take("a", t0).allowed);
    REQUIRE(limiter.Take("a", t0).allowed);
    REQUIRE_FALSE(limiter.Take("a", t0).allowed);

    auto t1 = t0 + 500ms;  // one token back
    REQUIRE(limiter.Take("a", t1).allowed);
    auto refused = limiter.Take("a", t1);
    REQUIRE_FALSE(refused.allowed);
    REQUIRE(refused.retry_after == 500ms);

    auto t2 = t1 + 1h;  // long idle: still only `burst`
    REQUIRE(limiter.Take("a", t2).allowed);
    REQUIRE(limiter.Take("a", t2).allowed);
    REQUIRE_FALSE(limiter.Take("a", t2).allowed);
}

TEST_CASE("A refused request does not take a token") {
    RateLimiter limiter({.rate_per_sec = 1, .burst = 1});
    auto t0 = Clock::now();
    REQUIRE(limiter.Take("a", t0).allowed);
    for (int i = 0; i < 10; i++) REQUIRE_FALSE(limiter.Take("a", t0 + 100ms).allowed);

    REQUIRE(limiter.Take("a", t0 + 1s).allowed);  // hammering did not push the refill back
}

TEST_CASE("Keys are limited independently") {
    RateLimiter limiter({.rate_per_sec = 1, .burst = 1});
    auto t0 = Clock::now();

    REQUIRE(limiter.Take("10.0.0.1", t0).allowed);
    REQUIRE_FALSE(limiter.Take("10.0.0.1", t0).allowed);
    REQUIRE(limiter.Take("10.0.0.2", t0).allowed);
}

TEST_CASE("Idle buckets are evicted once they would be full again") {
    // burst / rate = 2 s to refill from empty: after that an entry equals a fresh one.
    RateLimiter limiter({.rate_per_sec = 1, .burst = 2});
    auto t0 = Clock::now();
    limiter.Take("a", t0);
    limiter.Take("b", t0);
    REQUIRE(limiter.Size() == 2);

    limiter.Take("c", t0 + 1s);  // a and b not refilled yet: kept
    REQUIRE(limiter.Size() == 3);

    limiter.Take("c", t0 + 2500ms);  // a and b idle for 2.5 s: gone, c was used 1.5 s ago: kept
    REQUIRE(limiter.Size() == 1);

    // an evicted key starts again with a full bucket, as it would have had anyway
    REQUIRE(limiter.Take("a", t0 + 2500ms).allowed);
    REQUIRE(limiter.Take("a", t0 + 2500ms).allowed);
    REQUIRE_FALSE(limiter.Take("a", t0 + 2500ms).allowed);
}

TEST_CASE("Concurrent requests on one key never take more than the bucket holds") {
    RateLimiter limiter({.rate_per_sec = 0.001, .burst = 50});
    auto t0 = Clock::now();
    std::atomic<int> allowed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
        threads.emplace_back([&] {
            for (int i = 0; i < 100; i++)
                if (limiter.Take("a", t0).allowed) allowed++;
        });
    for (auto& t : threads) t.join();

    REQUIRE(allowed == 50);
}

// --- HTTP ---

TEST_CASE("Over the limit, any route gets 429 with Retry-After before its body is parsed") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};
    HttpServer server{svc, RateLimitConfig{.rate_per_sec = 1, .burst = 2}};
    std::thread server_thread([&] { server.Listen("127.0.0.1", 9094); });
    httplib::Client client{"localhost", 9094};
    client.set_connection_timeout(2);
    while (!server.IsRunning()) std::this_thread::sleep_for(10ms);

    auto first = client.Post("/jobs", R"({"payload":"{}"})", "application/json");
    auto second = client.Get("/jobs");
    auto limited_post = client.Post("/jobs", "not json", "application/json");  // 429, not 400
    auto limited_get = client.Get("/jobs");
    client.stop();
    server.Stop();
    server_thread.join();

    REQUIRE(first);
    REQUIRE(first->status == 201);
    REQUIRE(second);
    REQUIRE(second->status == 200);
    REQUIRE(limited_post);
    REQUIRE(limited_post->status == 429);
    REQUIRE(limited_post->get_header_value("Retry-After") == "1");
    REQUIRE(limited_get);
    REQUIRE(limited_get->status == 429);
    REQUIRE(test_db::CountWithStatus(conn, "PENDING") == 1);
}
