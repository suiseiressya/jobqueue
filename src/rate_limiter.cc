#include "rate_limiter.h"

#include <algorithm>

namespace {
// Refill arithmetic is in doubles; absorb rounding so N steps of 1/N still make a token.
constexpr double kEpsilon = 1e-9;
}  // namespace

RateLimiter::RateLimiter(RateLimitConfig config)
    : config_(config),
      idle_ttl_(std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(config.burst / config.rate_per_sec))),
      last_sweep_(Clock::now()) {}

RateLimiter::Decision RateLimiter::Take(std::string const& key, Clock::time_point now) {
    std::lock_guard guard(mut_);

    // TTL eviction, at most once per idle_ttl_: O(n) scan amortized over the requests between.
    if (now - last_sweep_ >= idle_ttl_) {
        std::erase_if(buckets_,
                      [&](auto const& entry) { return now - entry.second.last >= idle_ttl_; });
        last_sweep_ = now;
    }

    auto [it, inserted] = buckets_.try_emplace(key, Bucket{config_.burst, now});
    Bucket& bucket = it->second;

    if (now > bucket.last) {
        double elapsed = std::chrono::duration<double>(now - bucket.last).count();
        bucket.tokens = std::min(config_.burst, bucket.tokens + elapsed * config_.rate_per_sec);
        bucket.last = now;
    }

    if (bucket.tokens + kEpsilon >= 1.0) {
        bucket.tokens = std::max(0.0, bucket.tokens - 1.0);
        return {true, std::chrono::milliseconds{0}};
    }

    auto wait = std::chrono::duration<double>((1.0 - bucket.tokens) / config_.rate_per_sec);
    return {false, std::chrono::ceil<std::chrono::milliseconds>(wait)};
}

size_t RateLimiter::Size() {
    std::lock_guard guard(mut_);
    return buckets_.size();
}
