#include "PostLoginScanner.hpp"
#include "../util/Profile.hpp"

#include <iostream>

namespace dqxclarity
{

PostLoginScanner::PostLoginScanner(const ScannerCreateInfo& create_info)
    : ScannerBase(create_info)
{
    if (create_info.state_change_callback)
    {
        SetStateChangeCallback(create_info.state_change_callback);
    }
}

bool PostLoginScanner::OnInitialize()
{
    if (verbose_)
        std::cout << "PostLoginScanner: Initialized\n";

    return true;
}

bool PostLoginScanner::OnPoll()
{
    PROFILE_SCOPE_FUNCTION();

    uintptr_t pattern_addr = FindPattern(pattern_, false);
    bool found = (pattern_addr != 0);

    bool previous_state = is_logged_in_.exchange(found, std::memory_order_relaxed);

    if (found != previous_state)
    {
        if (verbose_)
            std::cout << "PostLoginScanner: State changed to " << (found ? "logged in" : "logged out") << "\n";

        if (state_change_callback_)
            state_change_callback_(found);

        return true;
    }

    return false;
}

} // namespace dqxclarity

