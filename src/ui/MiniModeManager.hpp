#pragma once

#include <imgui.h>

// Forward declarations
class AppContext;
class WindowRegistry;

namespace ui
{

/// Manages Mini mode-specific behavior and UI
class MiniModeManager
{
public:
    MiniModeManager(AppContext& app_context, WindowRegistry& registry);

    /// Handle Alt+Drag to move window in Mini mode
    void HandleAltDrag();

    /// Setup the dockspace container for Mini mode
    /// @return The ImGuiID of the created dockspace
    ImGuiID SetupDockspace();

    /// Restore dialog positions when leaving Mini mode
    void RestoreDialogsFromMiniMode();

private:
    AppContext& app_context_;
    WindowRegistry& registry_;
    bool drag_triggered_ = false;
};

} // namespace ui
