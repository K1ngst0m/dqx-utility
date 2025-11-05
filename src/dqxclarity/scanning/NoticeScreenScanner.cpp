#include "NoticeScreenScanner.hpp"
#include "../util/Profile.hpp"

#include <iostream>

namespace dqxclarity
{

NoticeScreenScanner::NoticeScreenScanner(const ScannerCreateInfo& create_info)
    : ScannerBase(create_info)
{
}

bool NoticeScreenScanner::OnInitialize()
{
    if (verbose_)
        std::cout << "NoticeScreenScanner: Initialized\n";

    return true;
}

bool NoticeScreenScanner::OnPoll()
{
    PROFILE_SCOPE_FUNCTION();

    uintptr_t pattern_addr = FindPattern(pattern_, false);
    bool found = (pattern_addr != 0);

    bool previous_state = is_visible_.exchange(found, std::memory_order_relaxed);

    if (found != previous_state)
    {
        if (verbose_)
            std::cout << "NoticeScreenScanner: State changed to " << (found ? "visible" : "hidden") << "\n";

        if (state_change_callback_)
            state_change_callback_(found);

        return true;
    }

    return false;
}

} // namespace dqxclarity

