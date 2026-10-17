#pragma once
#include <chrono>

std::chrono::milliseconds BackoffDelay(int retry_count);
