#include "backoff.h"
#include <random>

std::chrono::milliseconds BackoffDelay(int retry_count) {
    // One generator per thread to avoid data race / locks
    static thread_local std::mt19937 gen{std::random_device{}()};
    static constexpr long long d_0 = 1000, d_max = 60000;

    // 2^6 = 64 > 60 
    retry_count = std::clamp(retry_count, 0, 6);
    const long long d = std::min(d_max, d_0 << retry_count);
    std::uniform_int_distribution<long long> jitter(0, d / 2);

    return std::chrono::milliseconds{d + jitter(gen)};
}
