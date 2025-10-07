#pragma once

#include <imgui.h>

namespace DockState {
    ImGuiID GetDockspace();
    void SetDockspace(ImGuiID id);

    void BeginScatter(int frames);
    bool IsScattering();
    ImVec2 NextScatterPos();
    
    // Request windows to re-dock (used when switching modes)
    void RequestReDock();
    bool ShouldReDock();
    void ConsumeReDock();
    
    void EndFrame();
}
