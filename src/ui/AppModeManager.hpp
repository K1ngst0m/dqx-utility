#pragma once

#include "GlobalStateManager.hpp"

class AppContext;
class WindowRegistry;

namespace ui
{

class MiniModeManager;

class AppModeManager
{
public:
    AppModeManager(AppContext& app_context, WindowRegistry& registry, MiniModeManager& mini_manager);

    void ApplyModeSettings(GlobalStateManager::AppMode mode);
    void HandleModeChange(GlobalStateManager::AppMode old_mode, GlobalStateManager::AppMode new_mode);

    GlobalStateManager::AppMode GetCurrentMode() const { return current_mode_; }

    void SetCurrentMode(GlobalStateManager::AppMode mode) { current_mode_ = mode; }

private:
    AppContext& app_context_;
    [[maybe_unused]] WindowRegistry& registry_;
    MiniModeManager& mini_manager_;
    GlobalStateManager::AppMode current_mode_ = GlobalStateManager::AppMode::Normal;
};

} // namespace ui
