#include "AppModeManager.hpp"
#include "AppContext.hpp"
#include "WindowRegistry.hpp"
#include "MiniModeManager.hpp"
#include "DockState.hpp"

namespace ui {

AppModeManager::AppModeManager(AppContext& app_context, WindowRegistry& registry, MiniModeManager& mini_manager)
    : app_context_(app_context)
    , registry_(registry)
    , mini_manager_(mini_manager)
{
}

void AppModeManager::ApplyModeSettings(ConfigManager::AppMode mode)
{
    switch (mode)
    {
    case ConfigManager::AppMode::Mini:
        app_context_.setWindowBorderless(true);
        app_context_.setWindowAlwaysOnTop(true);
        app_context_.restoreWindow();
        app_context_.setWindowSize(600, 800);
        break;
        
    case ConfigManager::AppMode::Borderless:
        app_context_.setWindowBorderless(true);
        app_context_.setWindowAlwaysOnTop(false);
        app_context_.maximizeWindow();
        break;
        
    case ConfigManager::AppMode::Normal:
        app_context_.setWindowBorderless(false);
        app_context_.setWindowAlwaysOnTop(true);
        app_context_.restoreWindow();
        app_context_.setWindowSize(1024, 800);
        break;
    }
}

void AppModeManager::HandleModeChange(ConfigManager::AppMode old_mode, ConfigManager::AppMode new_mode)
{
    ApplyModeSettings(new_mode);
    
    if (old_mode == ConfigManager::AppMode::Mini && new_mode != ConfigManager::AppMode::Mini)
    {
        mini_manager_.RestoreDialogsFromMiniMode();
    }
    
    DockState::RequestReDock();
    current_mode_ = new_mode;
}

} // namespace ui
