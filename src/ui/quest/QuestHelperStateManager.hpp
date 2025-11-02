#pragma once

#include "common/BaseWindowState.hpp"

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

struct QuestHelperStateManager : BaseWindowState
{
    QuestHelperContentState quest_helper;

    void applyDefaults() override
    {
        BaseWindowState::applyDefaults();
        quest_helper.applyDefaults();
    }
};
