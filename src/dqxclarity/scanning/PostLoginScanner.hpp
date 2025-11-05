#pragma once

#include "ScannerBase.hpp"

#include <atomic>
#include <functional>

namespace dqxclarity
{

/**
 * @brief Scanner for detecting post-login state
 *
 * Continuously monitors for the walkthrough pattern that indicates
 * the player has successfully logged into the game world.
 */
class PostLoginScanner : public ScannerBase
{
public:
    explicit PostLoginScanner(const ScannerCreateInfo& create_info);
    ~PostLoginScanner() override = default;

    bool IsLoggedIn() const { return is_logged_in_.load(std::memory_order_relaxed); }

    void SetStateChangeCallback(std::function<void(bool)> callback)
    {
        state_change_callback_ = std::move(callback);
    }

protected:
    bool OnInitialize() override;
    bool OnPoll() override;

private:
    std::atomic<bool> is_logged_in_{ false };
    std::function<void(bool)> state_change_callback_;
};

} // namespace dqxclarity

