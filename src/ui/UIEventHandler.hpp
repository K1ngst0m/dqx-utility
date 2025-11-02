#pragma once

#include <imgui.h>

class AppContext;
class WindowRegistry;
class ConfigManager;

namespace ui
{

class UIEventHandler
{
public:
    UIEventHandler(AppContext& app_context, WindowRegistry& registry, ConfigManager& config);

    /// Check if mouse is currently outside all dialog windows
    bool IsMouseOutsideDialogs() const;

    /// Handle clicks on transparent areas to trigger vignette effect
    void HandleTransparentAreaClick();

    /// Render and handle the global right-click context menu
    void RenderGlobalContextMenu(bool& show_manager, bool& quit_requested);

private:
    AppContext& app_context_;
    WindowRegistry& registry_;
    ConfigManager& config_;
};

} // namespace ui
