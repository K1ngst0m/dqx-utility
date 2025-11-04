#pragma once

#include "HookBase.hpp"
#include "HookCreateInfo.hpp"
#include "../api/dqxclarity.hpp"

#include <string>

namespace dqxclarity
{

/**
 * @brief Hook for capturing player and sibling name information
 */
class PlayerHook : public HookBase
{
public:
    explicit PlayerHook(const HookCreateInfo& create_info);
    ~PlayerHook() = default;

    // Hook-specific polling
    bool PollPlayerData();

    const PlayerInfo& GetLastPlayer() const { return last_data_; }

protected:
    // HookBase pure virtual implementations
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;
    size_t ComputeStolenLength() override;

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxStringLength = 128;
    static constexpr size_t kDefaultStolenBytes = 6;

    // Offsets for player data
    static constexpr uint32_t kPlayerNameOffset = 24;
    static constexpr uint32_t kSiblingNameOffset = 100;
    static constexpr uint32_t kRelationshipOffset = 119;

    PlayerInfo last_data_;

    PlayerRelationship DecodeRelationship(uint8_t value) const;
};

} // namespace dqxclarity
