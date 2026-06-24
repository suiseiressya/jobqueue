#include <catch2/catch_test_macros.hpp>

#include "JobQueue.hpp"

TEST_CASE("JobQueue push and pop") {
    JobQueue q;
    auto id = JobId::generate();

    q.push(id);
    REQUIRE(q.size() == 1);
    REQUIRE(q.contains(id));

    auto popped = q.pop();
    REQUIRE(popped.has_value());
    REQUIRE(*popped == id);
    REQUIRE(q.size() == 0);
}

TEST_CASE("JobQueue pop empty returns nullopt") {
    JobQueue q;
    REQUIRE_FALSE(q.pop().has_value());
}

TEST_CASE("JobQueue top peeks without removing") {
    JobQueue q;

    REQUIRE_FALSE(q.top().has_value());

    auto id = JobId::generate();
    q.push(id);

    auto peeked = q.top();
    REQUIRE(peeked.has_value());
    REQUIRE(*peeked == id);
    REQUIRE(q.size() == 1);
}

TEST_CASE("JobQueue FIFO ordering") {
    JobQueue q;
    auto id1 = JobId::generate();
    auto id2 = JobId::generate();
    auto id3 = JobId::generate();

    q.push(id1);
    q.push(id2);
    q.push(id3);

    REQUIRE(*q.pop() == id1);
    REQUIRE(*q.pop() == id2);
    REQUIRE(*q.pop() == id3);
}

TEST_CASE("JobQueue push ignores duplicates") {
    JobQueue q;
    auto id = JobId::generate();

    q.push(id);
    q.push(id);
    REQUIRE(q.size() == 1);
}

TEST_CASE("JobQueue remove clears from active set") {
    JobQueue q;
    auto id = JobId::generate();

    q.push(id);
    REQUIRE(q.contains(id));

    q.remove(id);
    REQUIRE_FALSE(q.contains(id));
}

TEST_CASE("JobQueue remove allows re-push") {
    JobQueue q;
    auto id = JobId::generate();

    q.push(id);
    q.pop();
    q.remove(id);

    q.push(id);
    REQUIRE(q.size() == 1);
    REQUIRE(q.contains(id));
    REQUIRE(*q.pop() == id);
}

TEST_CASE("JobQueue mixed operations") {
    JobQueue q;
    auto a = JobId::generate();
    auto b = JobId::generate();
    auto c = JobId::generate();

    q.push(a);
    q.push(b);
    REQUIRE(*q.pop() == a);

    q.push(c);
    REQUIRE(q.size() == 2);
    REQUIRE(*q.pop() == b);
    REQUIRE(*q.pop() == c);
    REQUIRE(q.size() == 0);
}
