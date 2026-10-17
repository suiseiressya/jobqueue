// S4 slice 1: retry loop (D-019). A failed job goes back to PENDING with a backoff, the
// scheduler brings it back when due, and it ends FAILED once max_retries is used up.
// Waits are skipped by moving scheduled_at in SQL, except in the end-to-end test.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <limits>
#include <set>
#include <thread>

#include "backoff.h"
#include "job_repository.h"
#include "job_service.h"
#include "scheduler.h"
#include "test_db_helpers.h"
#include "test_log_helpers.h"
#include "thread_pool.h"

using namespace std::chrono_literals;

namespace {

// Poll until pred() is true or the timeout passes; returns the last result.
bool WaitUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return pred();
}

}  // namespace

// --- Repository (DB transitions) ---

TEST_CASE("Retry moves a RUNNING job back to PENDING with the error and a later scheduled_at") {
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, "{}", "RUNNING", "1 minute");
    JobRepository repo{conn};

    REQUIRE(repo.Retry(id, 10s, "boom"));

    auto job = repo.GetById(id);
    REQUIRE(job->job_status == kPending);
    REQUIRE(job->retry_count == 1);
    REQUIRE(job->error_message == "boom");
    double wait = test_db::SecondsUntilDue(conn, id);
    REQUIRE(wait > 9);
    REQUIRE(wait <= 10);

    // No longer RUNNING: the conditional update changes nothing (D-006).
    REQUIRE_FALSE(repo.Retry(id, 10s, "again"));
    REQUIRE(repo.GetById(id)->retry_count == 1);
}

TEST_CASE("GetDuePending returns due PENDING rows, earliest scheduled_at first, and skips future ones") {
    pqxx::connection conn{test_db::kConn};
    auto due_second = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    auto due_first = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    auto future = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    test_db::ScheduleIn(conn, due_second, -10);
    test_db::ScheduleIn(conn, due_first, -20);
    test_db::ScheduleIn(conn, future, 60);
    test_db::InsertJob(conn, "{}", "RUNNING", "1 minute");  // due, but not PENDING

    JobRepository repo{conn};
    auto due = repo.GetDuePendingJobs();

    REQUIRE(due.size() == 2);
    REQUIRE(due[0].id == due_first);
    REQUIRE(due[1].id == due_second);
}

// --- Backoff ---

TEST_CASE("BackoffDelay doubles from 1 s, caps at 60 s, and adds up to 50% jitter") {
    struct Case {
        int retry_count;
        std::chrono::milliseconds base;
    };
    const Case cases[] = {
        {0, 1s}, {1, 2s}, {2, 4s}, {5, 32s}, {6, 60s}, {7, 60s}, {1000, 60s},
        {std::numeric_limits<int>::max(), 60s},  // clamped, no overflow
        {-1, 1s}, {std::numeric_limits<int>::min(), 1s},  // bad row: treated as 0, no UB shift
    };
    for (const auto& c : cases) {
        INFO("retry_count " << c.retry_count);
        std::set<long long> seen;
        bool sub_second = false;
        for (int i = 0; i < 200; i++) {
            auto d = BackoffDelay(c.retry_count);
            INFO("gave " << d.count() << " ms");
            REQUIRE(d >= c.base);
            REQUIRE(d <= c.base * 3 / 2);
            seen.insert(d.count());
            if (d.count() % 1000 != 0) sub_second = true;
        }
        // Jitter is real for every count, the first retry included.
        REQUIRE(seen.size() > 1);
        // Millisecond resolution, not whole seconds: with 1 s steps the first retry
        // (d = 1 s, d/2 = 0 s) would get no jitter at all.
        REQUIRE(sub_second);
        // The whole [0, d/2] range is used: some draws in the lower and upper quarter.
        // Each check fails by chance with probability 2^-200.
        auto quarter = c.base.count() / 4;
        REQUIRE(*seen.begin() < c.base.count() + quarter);
        REQUIRE(*seen.rbegin() > c.base.count() + quarter);
    }
}

// --- Service: Fail decides retry vs FAILED, EnqueueDueJobs brings jobs back ---

TEST_CASE("Fail with retries left puts the job back to PENDING with a backoff and uncaches it") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};
    auto id = svc.Enqueue("{}");  // max_retries defaults to 3
    REQUIRE(svc.WaitAndPop().has_value());

    svc.RetryThenFail(id, "boom");

    auto job = svc.GetById(id);
    REQUIRE(job->job_status == kPending);
    REQUIRE(job->retry_count == 1);
    REQUIRE(job->error_message == "boom");
    double wait = test_db::SecondsUntilDue(conn, id);  // first retry: 1 s plus up to 0.5 s
    REQUIRE(wait > 0.9);
    REQUIRE(wait <= 1.5);
    // Nobody holds it now; the scheduler brings it back when due (D-019).
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("Fail with the retry budget used up marks the job FAILED and moves it to the DLQ") {
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    test_db::SetRetryState(conn, id, /*retry_count=*/3, /*max_retries=*/3);
    JobRepository repo{conn};
    JobService svc{repo};
    REQUIRE(svc.EnqueueDueJobs() == 1);
    REQUIRE(svc.WaitAndPop()->id == id);

    svc.RetryThenFail(id, "boom");

    auto job = svc.GetById(id);
    REQUIRE(job->job_status == kFailed);
    REQUIRE(job->retry_count == 3);
    REQUIRE(job->error_message == "boom");
    REQUIRE(test_db::InDlq(conn, id));  // slice 2: the final failure dead-letters it
    REQUIRE(svc.CacheSize() == 0);
}

TEST_CASE("EnqueueDue queues each due row once and skips rows not yet due") {
    pqxx::connection conn{test_db::kConn};
    auto due = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    auto future = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    test_db::ScheduleIn(conn, future, 60);
    JobRepository repo{conn};
    JobService svc{repo};
    svc.Enqueue("{}");  // due and already cached by the HTTP path

    REQUIRE(svc.EnqueueDueJobs() == 1);  // only `due`: `future` waits, the enqueued one is cached
    REQUIRE(svc.EnqueueDueJobs() == 0);  // no duplicates on the next tick (D-016)
    REQUIRE(svc.CacheSize() == 2);
}

// --- Scheduler thread + pool: the real path ---

// Decision D-020: startup recovery loads every PENDING row, due or not. A restart can run a
// backed-off job early; retry_count and max_retries still hold.
TEST_CASE("RestartRecovery loads PENDING rows even when they are not yet due") {
    pqxx::connection conn{test_db::kConn};
    auto backed_off = test_db::InsertJob(conn, "{}", "PENDING", "1 minute");
    test_db::ScheduleIn(conn, backed_off, 60);
    JobRepository repo{conn};
    JobService svc{repo};

    REQUIRE(svc.RestartRecovery() == 1);
    REQUIRE(svc.WaitAndPop()->id == backed_off);
}

TEST_CASE("Scheduler::Stop returns without waiting out the interval") {
    pqxx::connection conn{test_db::kConn};
    JobRepository repo{conn};
    JobService svc{repo};
    Scheduler scheduler{svc, 30s};
    scheduler.Start();
    std::this_thread::sleep_for(50ms);  // let it reach the wait

    auto start = std::chrono::steady_clock::now();
    scheduler.Stop();
    REQUIRE(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("Scheduler hands a PENDING row nobody holds to the pool") {
    // E.g. the row left behind when a claim threw (I-007): PENDING in the DB, not in the cache.
    pqxx::connection conn{test_db::kConn};
    auto orphan = test_db::InsertJob(conn, R"({"sleep_ms":10})", "PENDING", "1 minute");
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 1);
    Scheduler scheduler{svc, 50ms};
    pool.Start();
    scheduler.Start();

    bool done = WaitUntil([&] { return svc.GetById(orphan)->job_status == kDone; });

    scheduler.Stop();
    pool.Stop();
    REQUIRE(done);
}

TEST_CASE("A failing job is retried after its backoff and ends in the DLQ when the budget is used") {
    test_log::ExpectFailureLogs(2);
    pqxx::connection conn{test_db::kConn};
    auto id = test_db::InsertJob(conn, R"({"fail":true})", "PENDING", "1 minute");
    test_db::SetRetryState(conn, id, /*retry_count=*/0, /*max_retries=*/1);
    JobRepository repo{conn};
    JobService svc{repo};
    ThreadPool pool(svc, 1);
    Scheduler scheduler{svc, 50ms};
    pool.Start();
    scheduler.Start();

    // Run 1 fails -> PENDING, due in 1-1.5 s; run 2 fails -> FAILED. Real backoff, no shortcut.
    bool failed = WaitUntil([&] { return svc.GetById(id)->job_status == kFailed; }, 5s);

    scheduler.Stop();
    pool.Stop();
    REQUIRE(failed);
    auto job = svc.GetById(id);
    REQUIRE(job->retry_count == 1);
    REQUIRE_FALSE(job->error_message.empty());
    REQUIRE(test_db::InDlq(conn, id));
}
