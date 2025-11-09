#include "QuestScanner.hpp"
#include "ScannerBase.hpp"
#include "../util/Profile.hpp"

#include <iostream>
#include <array>

namespace dqxclarity
{

QuestScanner::QuestScanner(const ScannerCreateInfo& create_info)
    : ScannerBase(create_info)
{
}

bool QuestScanner::OnInitialize()
{
    PROFILE_SCOPE_FUNCTION();

    struct Candidate
    {
        std::array<uint8_t, 6> bytes;
        uint32_t name_off;
    } candidates[] = {
        { { 0xB6, 0x8F, 0x01, 0x00, 0x05, 0x00 }, 992u },
        { { 0xB6, 0x8F, 0x01, 0x00, 0x04, 0x00 }, 1064u },
    };

    for (const auto& c : candidates)
    {
        Pattern p = Pattern::FromBytes(c.bytes.data(), c.bytes.size());
        uintptr_t addr = FindPattern(p, false);
        if (addr != 0)
        {
            selected_pattern_ = p;
            name_offset_ = c.name_off;
            subname_offset_ = name_offset_ - 56u;
            description_offset_ = name_offset_ + 56u;
            if (logger_.info)
                logger_.info("QuestScanner: Initialized successfully");
            return true;
        }
    }

    if (logger_.warn)
        logger_.warn("QuestScanner: Pattern not found during init");
    return false;
}

bool QuestScanner::OnPoll()
{
    PROFILE_SCOPE_FUNCTION();

    uintptr_t base = FindPattern(selected_pattern_, false);
    if (base == 0)
        return false;

    std::string quest_name;
    if (!ReadString(base + name_offset_, quest_name))
        return false;

    if (quest_name.empty())
        return false;

    if (quest_name == last_quest_name_)
        return false;

    std::string subname;
    std::string desc;
    (void)ReadString(base + subname_offset_, subname);
    (void)ReadString(base + description_offset_, desc);

    last_quest_name_ = std::move(quest_name);
    last_subquest_name_ = std::move(subname);
    last_description_ = std::move(desc);
    last_time_ = std::chrono::steady_clock::now();

    if (verbose_)
    {
        std::cout << "QuestScanner: Captured quest: " << last_quest_name_.substr(0, 50)
                  << (last_quest_name_.length() > 50 ? "..." : "") << "\n";
    }

    return true;
}

} // namespace dqxclarity
