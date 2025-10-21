#pragma once

#include <atomic>
#include <chrono>

namespace dqxclarity
{

// Detect a post-login state by scanning for a heuristic pattern (e.g., walkthrough)
// Returns true if detected before cancellation/timeout.
bool DetectPostLogin(std::atomic<bool>& cancel,
                     std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250),
                     std::chrono::milliseconds timeout = std::chrono::seconds(5));

} // namespace dqxclarity