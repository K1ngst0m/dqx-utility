#pragma once

#include "ScannerBase.hpp"
#include "../api/player_info.hpp"

namespace dqxclarity
{

/**
 * @brief On-demand scanner for extracting player names
 *
 * Scans memory for the sibling name pattern and extracts
 * player name and sibling name from fixed offsets.
 * This scanner is not continuous - it's executed explicitly when needed.
 */
class PlayerNameScanner : public ScannerBase
{
public:
    explicit PlayerNameScanner(const ScannerCreateInfo& create_info);
    ~PlayerNameScanner() override = default;

    bool ScanPlayerInfo(PlayerInfo& out);

protected:
    bool OnInitialize() override;
    bool OnPoll() override;

private:
    static constexpr size_t kPlayerNameOffset = 21;
    static constexpr size_t kSiblingNameOffset = 51;
    static constexpr size_t kMaxNameLength = 128;
};

} // namespace dqxclarity

