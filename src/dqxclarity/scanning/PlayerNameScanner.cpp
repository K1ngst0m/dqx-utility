#include "PlayerNameScanner.hpp"
#include "../util/Profile.hpp"

#include <iostream>

namespace dqxclarity
{

PlayerNameScanner::PlayerNameScanner(const ScannerCreateInfo& create_info)
    : ScannerBase(create_info)
{
}

bool PlayerNameScanner::OnInitialize()
{
    if (verbose_)
        std::cout << "PlayerNameScanner: Initialized\n";

    return true;
}

bool PlayerNameScanner::OnPoll()
{
    return false;
}

bool PlayerNameScanner::ScanPlayerInfo(PlayerInfo& out)
{
    PROFILE_SCOPE_FUNCTION();

    if (!IsActive())
    {
        if (!Initialize())
            return false;
    }

    uintptr_t pattern_addr = FindPattern(pattern_, false);
    if (pattern_addr == 0)
    {
        if (verbose_)
            std::cout << "PlayerNameScanner: Pattern not found\n";
        return false;
    }

    std::string player_name;
    std::string sibling_name;

    if (!ReadString(pattern_addr - kPlayerNameOffset, player_name, kMaxNameLength))
        return false;

    if (!ReadString(pattern_addr + kSiblingNameOffset, sibling_name, kMaxNameLength))
        return false;

    if (!player_name.empty() && static_cast<unsigned char>(player_name.front()) < 0x20)
        player_name.erase(player_name.begin());

    if (!sibling_name.empty() && static_cast<unsigned char>(sibling_name.front()) < 0x20)
        sibling_name.erase(sibling_name.begin());

    if (player_name.empty() || sibling_name.empty())
        return false;

    out.player_name = std::move(player_name);
    out.sibling_name = std::move(sibling_name);
    out.relationship = PlayerRelationship::Unknown;

    if (verbose_)
        std::cout << "PlayerNameScanner: Captured player=\"" << out.player_name 
                  << "\" sibling=\"" << out.sibling_name << "\"\n";

    if (logger_.info)
        logger_.info("PlayerNameScanner: Captured player=\"" + out.player_name + 
                     "\" sibling=\"" + out.sibling_name + "\"");

    return true;
}

} // namespace dqxclarity

