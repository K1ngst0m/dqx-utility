#pragma once

#include "config/ConfigManager.hpp"

class AppContext;
class WindowRegistry;

namespace ui
{

class MiniModeManager;

class AppModeManager
{
public:
    AppModeManager(AppContext& app_context, WindowRegistry& registry, MiniModeManager& mini_manager);

    void ApplyModeSettings(ConfigManager::AppMode mode);
    void HandleModeChange(ConfigManager::AppMode old_mode, ConfigManager::AppMode new_mode);

    ConfigManager::AppMode GetCurrentMode() const { return current_mode_; }

    void SetCurrentMode(ConfigManager::AppMode mode) { current_mode_ = mode; }

private:
    AppContext& app_context_;
    WindowRegistry& registry_;
    MiniModeManager& mini_manager_;
    ConfigManager::AppMode current_mode_ = ConfigManager::AppMode::Normal;
};

} // namespace ui
