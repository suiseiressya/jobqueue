#include <catch2/catch_test_macros.hpp>

#include "job_queue.h"

TEST_CASE("JobQueue push and pop") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    REQUIRE(q.Size() == 1);

    auto popped = q.Pop();
    REQUIRE(popped.has_value());
    REQUIRE(*popped == id);
    REQUIRE(q.Size() == 0);
}

TEST_CASE("JobQueue pop empty returns nullopt") {
    JobQueue q;
    REQUIRE_FALSE(q.Pop().has_value());
}

TEST_CASE("JobQueue top peeks without removing") {
    JobQueue q;

    REQUIRE_FALSE(q.Top().has_value());

    auto id = JobId::Generate();
    q.Push(id);

    auto peeked = q.Top();
    REQUIRE(peeked.has_value());
    REQUIRE(*peeked == id);
    REQUIRE(q.Size() == 1);
}

TEST_CASE("JobQueue FIFO ordering") {
    JobQueue q;
    auto id1 = JobId::Generate();
    auto id2 = JobId::Generate();
    auto id3 = JobId::Generate();

    q.Push(id1);
    q.Push(id2);
    q.Push(id3);

    REQUIRE(*q.Pop() == id1);
    REQUIRE(*q.Pop() == id2);
    REQUIRE(*q.Pop() == id3);
}

TEST_CASE("JobQueue push keep duplicates. Duplicate is handled by JobService") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    q.Push(id);
    REQUIRE(q.Size() == 2);
}

TEST_CASE("JobQueue mixed operations") {
    JobQueue q;
    auto a = JobId::Generate();
    auto b = JobId::Generate();
    auto c = JobId::Generate();

    q.Push(a);
    q.Push(b);
    REQUIRE(*q.Pop() == a);

    q.Push(c);
    REQUIRE(q.Size() == 2);
    REQUIRE(*q.Pop() == b);
    REQUIRE(*q.Pop() == c);
    REQUIRE(q.Size() == 0);
}
