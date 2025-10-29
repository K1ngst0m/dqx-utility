#pragma once

#include <cstdint>
#include <string>

namespace dqxclarity
{

enum class PlayerRelationship : std::uint8_t
{
    Unknown = 0,
    OlderBrother = 0x01,
    YoungerBrother = 0x02,
    OlderSister = 0x03,
    YoungerSister = 0x04
};

struct PlayerInfo
{
    std::string player_name;
    std::string sibling_name;
    PlayerRelationship relationship = PlayerRelationship::Unknown;
    std::uint64_t seq = 0;
};

} // namespace dqxclarity
