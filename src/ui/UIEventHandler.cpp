#include "UIEventHandler.hpp"
#include "AppContext.hpp"
#include "WindowRegistry.hpp"
#include "dialog/DialogWindow.hpp"
#include "config/ConfigManager.hpp"
#include "Localization.hpp"

namespace ui {

UIEventHandler::UIEventHandler(AppContext& app_context, WindowRegistry& registry)
    : app_context_(app_context)
    , registry_(registry)
{
}

bool UIEventHandler::IsMouseOutsideDialogs(ImGuiIO& io) const
{
    if (!ImGui::IsMousePosValid(&io.MousePos))
        return false;

    auto dialogs = registry_.windowsByType(UIWindowType::Dialog);
    for (auto* window : dialogs)
    {
        auto* dialog = dynamic_cast<DialogWindow*>(window);
        if (dialog)
        {
            const auto& state = dialog->state();
            bool within_dialog = ImGui::IsMouseHoveringRect(
                state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y),
                false);
            if (within_dialog)
                return false;
        }
    }
    return true;
}

void UIEventHandler::HandleTransparentAreaClick(ImGuiIO& io)
{
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;
    
    if (io.WantCaptureMouse)
        return;
    
    if (!IsMouseOutsideDialogs(io))
        return;
    
    app_context_.triggerVignette(io.MousePos.x, io.MousePos.y);
}

void UIEventHandler::RenderGlobalContextMenu(ImGuiIO& io, bool& show_manager, bool& quit_requested)
{
    if (IsMouseOutsideDialogs(io) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("GlobalContextMenu");
    }

    if (ImGui::BeginPopup("GlobalContextMenu"))
    {
        if (ImGui::MenuItem(i18n::get("menu.global_settings")))
            show_manager = true;

        if (ImGui::BeginMenu(i18n::get("menu.app_mode")))
        {
            if (auto* cm = ConfigManager_Get())
            {
                auto mode = cm->getAppMode();
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.normal"), nullptr, mode == ConfigManager::AppMode::Normal))
                    cm->setAppMode(ConfigManager::AppMode::Normal);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.borderless"), nullptr, mode == ConfigManager::AppMode::Borderless))
                    cm->setAppMode(ConfigManager::AppMode::Borderless);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.mini"), nullptr, mode == ConfigManager::AppMode::Mini))
                    cm->setAppMode(ConfigManager::AppMode::Mini);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(i18n::get("menu.quit")))
            quit_requested = true;
        
        ImGui::EndPopup();
    }
}

} // namespace ui
