#include "DockState.hpp"
#include <imgui.h>

namespace
{
ImGuiID g_dockspace = 0;
int g_scatter_frames = 0;
int g_scatter_index = 0;
bool g_should_redock = false;
} // namespace

namespace DockState
{
ImGuiID GetDockspace() { return g_dockspace; }

void SetDockspace(ImGuiID id) { g_dockspace = id; }

void BeginScatter(int frames)
{
    if (frames < 0)
        frames = 0;
    g_scatter_frames = frames;
    g_scatter_index = 0;
}

bool IsScattering() { return g_scatter_frames > 0; }

ImVec2 NextScatterPos()
{
    ImVec2 start(60.0f, 60.0f);
    ImVec2 step(40.0f, 36.0f);
    ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;
    ImVec2 pos = ImVec2(vp_pos.x + start.x + (g_scatter_index % 9) * step.x,
                        vp_pos.y + start.y + (g_scatter_index % 9) * step.y);
    g_scatter_index++;
    return pos;
}

void RequestReDock() { g_should_redock = true; }

bool ShouldReDock() { return g_should_redock; }

void ConsumeReDock() { g_should_redock = false; }

void EndFrame()
{
    if (g_scatter_frames > 0)
        g_scatter_frames--;
    g_scatter_index = 0;
}
} // namespace DockState
