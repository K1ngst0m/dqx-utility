#pragma once

#include "DialogStateManager.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct QuestHelperContentState
{
    std::string quest_id;
    std::string quest_name;
    std::uint64_t seq = 0;

    void applyDefaults()
    {
        quest_id.clear();
        quest_name.clear();
        seq = 0;
    }
};

struct QuestHelperStateManager : DialogStateManager
{
    QuestHelperContentState quest_helper;

    void applyDefaults()
    {
        DialogStateManager::applyDefaults();
        quest_helper.applyDefaults();
    }
};
