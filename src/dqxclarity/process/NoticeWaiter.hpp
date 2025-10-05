#pragma once

#include <atomic>
#include <chrono>

namespace dqxclarity {

// Waits for DQXGame.exe's "Important notice" screen to be present in memory
// by scanning for the UTF-8 notice string pattern across readable memory.
// Returns true if found before cancellation/timeout, false otherwise.
bool WaitForNoticeScreen(
    std::atomic<bool>& cancel,
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250),
    std::chrono::milliseconds timeout = std::chrono::minutes(5)
);

} // namespace dqxclarity