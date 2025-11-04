#pragma once

#include "HookBase.hpp"
#include "HookCreateInfo.hpp"

#include <string>

namespace dqxclarity
{

/**
 * @brief Hook for capturing quest text data
 * 
 * Captures quest metadata including title, description, objectives, and rewards.
 */
class QuestHook : public HookBase
{
public:
    struct QuestData
    {
        std::string subquest_name;
        std::string quest_name;
        std::string description;
        std::string rewards;
        std::string repeat_rewards;
    };

    explicit QuestHook(const HookCreateInfo& create_info);
    ~QuestHook() = default;

    // Hook-specific polling
    bool PollQuestData();

    const QuestData& GetLastQuest() const { return last_data_; }

protected:
    // HookBase pure virtual implementations
    Pattern GetSignature() const override;
    std::vector<uint8_t> GenerateDetourPayload() override;
    size_t ComputeStolenLength() override;

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxStringLength = 2048;
    static constexpr size_t kDefaultStolenBytes = 6;

    // Offsets for quest data in backup buffer
    static constexpr uint32_t kSubquestNameOffset = 20;
    static constexpr uint32_t kQuestNameOffset = 76;
    static constexpr uint32_t kDescriptionOffset = 132;
    static constexpr uint32_t kRewardsOffset = 640;
    static constexpr uint32_t kRepeatRewardsOffset = 744;

    QuestData last_data_;
};

} // namespace dqxclarity
