#pragma once

#include <imgui.h>

class AppContext;
class WindowRegistry;
class GlobalStateManager;
class ConfigManager;

namespace ui
{

class UIEventHandler
{
public:
    UIEventHandler(AppContext& context, WindowRegistry& registry, GlobalStateManager& global_state, ConfigManager& config);

    /// Check if mouse is currently outside all dialog windows
    bool IsMouseOutsideDialogs() const;

    /// Handle clicks on transparent areas to trigger vignette effect
    void HandleTransparentAreaClick();

    /// Render and handle the global right-click context menu
    void RenderGlobalContextMenu(bool& show_manager, bool& quit_requested);

private:
    AppContext& context_;
    WindowRegistry& registry_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
};

} // namespace ui
