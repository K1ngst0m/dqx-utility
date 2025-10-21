#pragma once

#include <cstdint>
#include <string>

namespace dqxclarity
{

struct QuestMessage
{
    std::uint64_t seq = 0;
    std::string subquest_name;
    std::string quest_name;
    std::string description;
    std::string rewards;
    std::string repeat_rewards;
};

} // namespace dqxclarity
