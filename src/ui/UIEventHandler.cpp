#include "UIEventHandler.hpp"
#include "AppContext.hpp"
#include "WindowRegistry.hpp"
#include "dialog/DialogWindow.hpp"
#include "quest/QuestWindow.hpp"
#include "help/HelpWindow.hpp"
#include "config/ConfigManager.hpp"
#include "Localization.hpp"
#include "CommonUIComponents.hpp"

#include <string>

namespace ui {

UIEventHandler::UIEventHandler(AppContext& app_context, WindowRegistry& registry)
    : app_context_(app_context)
    , registry_(registry)
{
}

bool UIEventHandler::IsMouseOutsideDialogs() const
{
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid(&io.MousePos))
        return false;

    auto is_mouse_over = [](UIWindow* window) -> bool
    {
        if (auto* dialog = dynamic_cast<DialogWindow*>(window))
        {
            const auto& state = dialog->state();
            return ImGui::IsMouseHoveringRect(
                state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y),
                false);
        }
        if (auto* quest = dynamic_cast<QuestWindow*>(window))
        {
            const auto& state = quest->state();
            return ImGui::IsMouseHoveringRect(
                state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y),
                false);
        }
        if (auto* help = dynamic_cast<HelpWindow*>(window))
        {
            const auto& state = help->state();
            return ImGui::IsMouseHoveringRect(
                state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y),
                false);
        }
        return false;
    };

    auto dialogs = registry_.windowsByType(UIWindowType::Dialog);
    for (auto* window : dialogs)
    {
        if (is_mouse_over(window))
            return false;
    }

    auto quests = registry_.windowsByType(UIWindowType::Quest);
    for (auto* window : quests)
    {
        if (is_mouse_over(window))
            return false;
    }

    auto helps = registry_.windowsByType(UIWindowType::Help);
    for (auto* window : helps)
    {
        if (is_mouse_over(window))
            return false;
    }

    return true;
}

void UIEventHandler::HandleTransparentAreaClick()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    if (io.WantCaptureMouse)
        return;

    if (!IsMouseOutsideDialogs())
        return;

    app_context_.triggerVignette(io.MousePos.x, io.MousePos.y);
}

void UIEventHandler::RenderGlobalContextMenu(bool& show_manager, bool& quit_requested)
{
    if (IsMouseOutsideDialogs() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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

        if (auto* cm = ConfigManager_Get())
        {
            std::string defaults_menu_label = ui::LocalizedOrFallback("menu.default_windows", "Default windows");
            if (ImGui::BeginMenu(defaults_menu_label.c_str()))
            {
                bool dialog_enabled = cm->isDefaultDialogEnabled();
                std::string dialog_label = ui::LocalizedOrFallback("menu.default_dialog", "Default dialog window");
                if (ImGui::MenuItem(dialog_label.c_str(), nullptr, dialog_enabled))
                {
                    cm->setDefaultDialogEnabled(!dialog_enabled);
                }

                bool quest_enabled = cm->isDefaultQuestEnabled();
                std::string quest_label = ui::LocalizedOrFallback("menu.default_quest", "Default quest window");
                if (ImGui::MenuItem(quest_label.c_str(), nullptr, quest_enabled))
                {
                    cm->setDefaultQuestEnabled(!quest_enabled);
                }

                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem(i18n::get("menu.quit")))
            quit_requested = true;

        ImGui::EndPopup();
    }
}

} // namespace ui
