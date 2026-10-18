#pragma once
#include <string>

/// Upper bound on sleep_ms: without it, a few jobs with a huge sleep hold every worker for days.
inline constexpr int kMaxSleepMs = 60'000;

/// Fake-work spec carried in a job's payload (D-015): {"sleep_ms": 0..kMaxSleepMs, "fail": bool},
/// both optional; other keys are ignored.
struct WorkSpec {
    int sleep_ms = 0;
    bool fail = false;
};

/// Parse and validate a payload. Throws std::invalid_argument with the reason if it is not
/// a valid spec. Used by POST /jobs (400) and by the worker, so the two cannot drift apart.
WorkSpec ParsePayload(const std::string& payload);
