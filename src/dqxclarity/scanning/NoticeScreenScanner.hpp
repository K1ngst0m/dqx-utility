#pragma once

#include "ScannerBase.hpp"

#include <atomic>
#include <functional>

namespace dqxclarity
{

/**
 * @brief Scanner for detecting the notice/login screen
 *
 * Continuously monitors for the notice screen pattern in memory.
 * Provides state tracking and callbacks for state changes.
 */
class NoticeScreenScanner : public ScannerBase
{
public:
    explicit NoticeScreenScanner(const ScannerCreateInfo& create_info);
    ~NoticeScreenScanner() override = default;

    bool IsVisible() const { return is_visible_.load(std::memory_order_relaxed); }

    void SetStateChangeCallback(std::function<void(bool)> callback)
    {
        state_change_callback_ = std::move(callback);
    }

protected:
    bool OnInitialize() override;
    bool OnPoll() override;

private:
    std::atomic<bool> is_visible_{ false };
    std::function<void(bool)> state_change_callback_;
};

} // namespace dqxclarity

