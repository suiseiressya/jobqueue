#include <catch2/catch_test_macros.hpp>

#include "job_queue.h"

TEST_CASE("JobQueue push and pop") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    REQUIRE(q.Size() == 1);
    REQUIRE(q.Contains(id));

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

TEST_CASE("JobQueue push ignores duplicates") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    q.Push(id);
    REQUIRE(q.Size() == 1);
}

TEST_CASE("JobQueue remove clears from active set") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    REQUIRE(q.Contains(id));

    q.Remove(id);
    REQUIRE_FALSE(q.Contains(id));
}

TEST_CASE("JobQueue remove allows re-push") {
    JobQueue q;
    auto id = JobId::Generate();

    q.Push(id);
    q.Pop();
    q.Remove(id);

    q.Push(id);
    REQUIRE(q.Size() == 1);
    REQUIRE(q.Contains(id));
    REQUIRE(*q.Pop() == id);
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
