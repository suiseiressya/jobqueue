#pragma once
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <iostream>

namespace test_log {

// Call at the start of a test that triggers failure paths on purpose, so the
// error logs it produces can be traced back to the test in `make test` output.
inline void ExpectFailureLogs(int count) {
    std::cerr << std::endl << "[test] \"" << Catch::getResultCapture().getCurrentTestName()
              << "\": expect " << count << " failure log line(s) below" << std::endl;
}

// For tests whose failure logs depend on thread timing (e.g. races), where the count varies
// per run and may be zero.
inline void ExpectTimingDependentLogs(const char* what) {
    std::cerr << std::endl << "[test] \"" << Catch::getResultCapture().getCurrentTestName()
              << "\": expect 0 or more log line(s) below, count depends on timing: " << what
              << std::endl;
}

}  // namespace test_log
