#pragma once

#include "DialogStateManager.hpp"

#include <cstdint>
#include <string>

struct QuestContentState
{
    std::string subquest_name;
    std::string quest_name;
    std::string description;
    std::string rewards;
    std::string repeat_rewards;
    std::uint64_t seq = 0;

    void applyDefaults()
    {
        subquest_name.clear();
        quest_name.clear();
        description.clear();
        rewards.clear();
        repeat_rewards.clear();
        seq = 0;
    }
};

struct QuestTranslationState
{
    std::string subquest_name;
    std::string quest_name;
    std::string description;
    std::string rewards;
    std::string repeat_rewards;

    void applyDefaults()
    {
        subquest_name.clear();
        quest_name.clear();
        description.clear();
        rewards.clear();
        repeat_rewards.clear();
    }
};

struct QuestStateManager : DialogStateManager
{
    QuestContentState quest;
    QuestTranslationState translated;
    QuestTranslationState original;
    bool translation_valid = false;
    bool translation_failed = false;
    std::string translation_error;

    void applyDefaults()
    {
        DialogStateManager::applyDefaults();
        quest.applyDefaults();
        translated.applyDefaults();
        original.applyDefaults();
        translation_valid = false;
        translation_failed = false;
        translation_error.clear();
    }
};
