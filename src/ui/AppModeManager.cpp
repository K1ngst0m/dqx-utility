#include "AppModeManager.hpp"
#include "AppContext.hpp"
#include "WindowRegistry.hpp"
#include "MiniModeManager.hpp"
#include "DockState.hpp"

namespace ui
{

AppModeManager::AppModeManager(AppContext& app_context, WindowRegistry& registry, MiniModeManager& mini_manager)
    : app_context_(app_context)
    , registry_(registry)
    , mini_manager_(mini_manager)
{
}

void AppModeManager::ApplyModeSettings(GlobalStateManager::AppMode mode)
{
    switch (mode)
    {
    case GlobalStateManager::AppMode::Mini:
        app_context_.setWindowBorderless(true);
        app_context_.setWindowAlwaysOnTop(true);
        app_context_.restoreWindow();
        app_context_.setWindowSize(600, 800);
        break;

    case GlobalStateManager::AppMode::Borderless:
        app_context_.setWindowBorderless(true);
        app_context_.setWindowAlwaysOnTop(false);
        app_context_.maximizeWindow();
        break;

    case GlobalStateManager::AppMode::Normal:
        app_context_.setWindowBorderless(false);
        app_context_.setWindowAlwaysOnTop(true);
        app_context_.restoreWindow();
        app_context_.setWindowSize(1024, 800);
        break;
    }
}

void AppModeManager::HandleModeChange(GlobalStateManager::AppMode old_mode, GlobalStateManager::AppMode new_mode)
{
    ApplyModeSettings(new_mode);

    if (old_mode == GlobalStateManager::AppMode::Mini && new_mode != GlobalStateManager::AppMode::Mini)
    {
        mini_manager_.RestoreDialogsFromMiniMode();
    }

    DockState::RequestReDock();
    current_mode_ = new_mode;
}

} // namespace ui
