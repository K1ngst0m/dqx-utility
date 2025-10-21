#pragma once

#include <imgui.h>

// Forward declarations
class AppContext;
class WindowRegistry;

namespace ui
{

/// Handles UI-related event processing and interactions
class UIEventHandler
{
public:
    UIEventHandler(AppContext& app_context, WindowRegistry& registry);

    /// Check if mouse is currently outside all dialog windows
    bool IsMouseOutsideDialogs() const;

    /// Handle clicks on transparent areas to trigger vignette effect
    void HandleTransparentAreaClick();

    /// Render and handle the global right-click context menu
    void RenderGlobalContextMenu(bool& show_manager, bool& quit_requested);

private:
    AppContext& app_context_;
    WindowRegistry& registry_;
};

} // namespace ui
