#pragma once

#include "ui/common/BaseWindowState.hpp"

struct MonsterStateManager : BaseWindowState
{
    void applyDefaults() override
    {
        BaseWindowState::applyDefaults();
        ui.width = 600.0f;
        ui.height = 700.0f;
        ui.window_size = ImVec2(ui.width, ui.height);
        ui.pending_resize = true;
        ui.pending_reposition = true;
    }
};
