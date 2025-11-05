#include "DialogScanner.hpp"
#include "../util/Profile.hpp"

#include <iostream>

namespace dqxclarity
{

DialogScanner::DialogScanner(const ScannerCreateInfo& create_info)
    : ScannerBase(create_info)
{
}

bool DialogScanner::OnInitialize()
{
    PROFILE_SCOPE_FUNCTION();

    if (verbose_)
        std::cout << "DialogScanner: Initializing...\n";

    uintptr_t pattern_addr = FindPattern(pattern_, false);
    if (pattern_addr != 0)
    {
        if (verbose_)
        {
            std::cout << "DialogScanner: Pattern found at 0x" << std::hex << pattern_addr << std::dec << "\n";
        }
        if (logger_.info)
            logger_.info("DialogScanner: Initialized successfully");
        return true;
    }
    else
    {
        if (logger_.warn)
            logger_.warn("DialogScanner: Pattern not found during init");
        return false;
    }
}

bool DialogScanner::OnPoll()
{
    PROFILE_SCOPE_FUNCTION();

    try
    {
        auto now = std::chrono::steady_clock::now();

        uintptr_t pattern_addr = FindPattern(pattern_, false);
        if (pattern_addr == 0)
        {
            return false;
        }

        uintptr_t dialog_base_addr = pattern_addr + kPointerOffset;
        uint32_t dialog_actual_addr = 0;

        if (!memory_->ReadMemory(dialog_base_addr, &dialog_actual_addr, sizeof(dialog_actual_addr)))
        {
            if (verbose_)
                std::cout << "DialogScanner: Failed to read pointer at 0x" << std::hex << dialog_base_addr
                          << std::dec << "\n";
            return false;
        }

        if (dialog_actual_addr == 0)
        {
            return false;
        }

        std::string text;
        if (!ReadString(static_cast<uintptr_t>(dialog_actual_addr), text))
        {
            if (verbose_)
                std::cout << "DialogScanner: Failed to read dialog text at 0x" << std::hex << dialog_actual_addr
                          << std::dec << "\n";
            return false;
        }

        if (text == last_dialog_text_)
        {
            return false;
        }

        if (text.empty())
        {
            return false;
        }

        last_dialog_text_ = text;
        last_dialog_time_ = now;
        last_npc_name_ = "No_NPC";

        if (verbose_)
        {
            std::cout << "DialogScanner: Captured text: " << text.substr(0, 50)
                      << (text.length() > 50 ? "..." : "") << "\n";
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace dqxclarity

