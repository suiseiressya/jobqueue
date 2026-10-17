#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

/// Token bucket settings, per key (client IP). See D-024.
struct RateLimitConfig {
    double rate_per_sec;  // tokens added per second
    double burst;         // bucket size: requests allowed at once after idling
};

/**
Token bucket per key. Thread-safe: every Take() runs under one mutex (httplib serves
requests on many threads). Independent of JobService's lock (D-004 is about JobQueue).
*/
class RateLimiter {
public:
    /// steady_clock: a wall-clock jump (NTP, manual change) cannot refill or drain a bucket
    using Clock = std::chrono::steady_clock;

    struct Decision {
        bool allowed;
        std::chrono::milliseconds retry_after;  // 0 if allowed, else time until one token
    };

    explicit RateLimiter(RateLimitConfig config);

    /// Take one token for `key` if the bucket has one; a refused request takes nothing.
    Decision Take(std::string const& key, Clock::time_point now = Clock::now());

    /// Number of tracked keys
    size_t Size();

private:
    struct Bucket {
        double tokens;
        Clock::time_point last;  // last refill
    };

    RateLimitConfig config_;

    /// An entry idle this long has refilled to `burst`, the same as a fresh one: evicting it
    /// changes no decision. Bounds the map to keys seen within one refill time.
    Clock::duration idle_ttl_;

    std::mutex mut_;
    std::unordered_map<std::string, Bucket> buckets_;
    Clock::time_point last_sweep_;
};
