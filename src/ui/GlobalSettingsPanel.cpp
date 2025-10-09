#include "GlobalSettingsPanel.hpp"

#include "dialog/DialogWindow.hpp"
#include "ProcessDetector.hpp"
#include "ProcessLocaleChecker.hpp"
#include "DQXClarityLauncher.hpp"
#include "config/ConfigManager.hpp"
#include "UITheme.hpp"
#include "DQXClarityService.hpp"
#include "dqxclarity/api/dqxclarity.hpp"
#include "ui/Localization.hpp"
#include "ui/DockState.hpp"

#include <algorithm>
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace
{
    struct WindowTypeEntry
    {
        UIWindowType type;
        const char*  label;
    };

    constexpr WindowTypeEntry kWindowTypes[] = {
        {UIWindowType::Dialog, "Dialog"}
    };
}

// Builds a settings panel tied to the window registry.
GlobalSettingsPanel::GlobalSettingsPanel(WindowRegistry& registry)
    : registry_(registry)
    , dqxc_launcher_(std::make_unique<DQXClarityLauncher>())
{
    // Expose launcher globally for UI windows to fetch dialog messages
    DQXClarityService_Set(dqxc_launcher_.get());
}

// Renders the settings window with type/instance selectors.
void GlobalSettingsPanel::render(bool& open)
{
    if (!open)
        return;

    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    else if (auto* cm = ConfigManager_Get())
    {
        if (cm->getAppMode() == ConfigManager::AppMode::Mini)
        {
            ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
        }
    }

    if (auto* cm = ConfigManager_Get())
    {
        if (cm->getAppMode() != ConfigManager::AppMode::Mini && DockState::ShouldReDock())
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_Always);
        }
        else
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_FirstUseEver);
        }
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_FirstUseEver);
    }
    UITheme::pushSettingsWindowStyle();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (auto* cm2 = ConfigManager_Get())
    {
        if (cm2->getAppMode() == ConfigManager::AppMode::Mini)
            flags |= ImGuiWindowFlags_NoMove;
    }
    if (ImGui::Begin((std::string(i18n::get("settings.title")) + "###global_settings").c_str(), &open, flags))
    {
        renderStatusSection();

        renderAppearanceSection();

        renderWindowManagementSection();
        
        if (ImGui::CollapsingHeader(i18n::get("settings.sections.debug")))
        {
            renderDebugSection();
        }
    }
    ImGui::End();

    UITheme::popSettingsWindowStyle();
}

// Provides a combo box for selecting the active window type.
void GlobalSettingsPanel::renderTypeSelector()
{
    int current_index = 0;
    for (int i = 0; i < static_cast<int>(std::size(kWindowTypes)); ++i)
    {
        if (kWindowTypes[i].type == selected_type_)
        {
            current_index = i;
            break;
        }
    }

    const char* preview = i18n::get("window_type.dialog");
    if (ImGui::BeginCombo("##window_type_combo", preview))
    {
        for (int i = 0; i < static_cast<int>(std::size(kWindowTypes)); ++i)
        {
            const bool selected = (i == current_index);
            const char* label = i18n::get("window_type.dialog");
            if (ImGui::Selectable(label, selected))
            {
                selected_type_ = kWindowTypes[i].type;
                selected_index_ = 0;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Displays instance list and creation controls for the active type.
void GlobalSettingsPanel::renderInstanceSelector(const std::vector<UIWindow*>& windows)
{
    if (selected_type_ == UIWindowType::Dialog)
    {
        if (ImGui::Button(i18n::get("settings.add_dialog")))
        {
            registry_.createDialogWindow();
            auto filtered = registry_.windowsByType(UIWindowType::Dialog);
            selected_index_ = static_cast<int>(filtered.size()) - 1;
            previous_selected_index_ = -1;
        }
        ImGui::SameLine();
        {
            std::string total = i18n::format("total", {{"count", std::to_string(windows.size())}});
            ImGui::TextDisabled("%s", total.c_str());
        }
    }

    if (windows.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", i18n::get("settings.no_instances"));
        return;
    }

    if (selected_index_ >= static_cast<int>(windows.size()))
        selected_index_ = static_cast<int>(windows.size()) - 1;

    if (ImGui::BeginTable("InstanceTable", 3, ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn(i18n::get("settings.table.name"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(i18n::get("settings.table.type"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(i18n::get("settings.table.actions"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(windows.size()); ++i)
        {
            UIWindow* win = windows[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool selected = (i == selected_index_);
            if (ImGui::Selectable(win->displayName(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                selected_index_ = i;
            }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(i18n::get("window_type.dialog"));

            ImGui::TableSetColumnIndex(2);
            std::string remove_id = std::string(i18n::get("common.remove")) + "##" + win->windowLabel();
            if (ImGui::SmallButton(remove_id.c_str()))
            {
                registry_.removeWindow(win);
                
                // Update the windows list after removal
                auto updated_windows = registry_.windowsByType(selected_type_);
                
                // Reset state after removal
                if (updated_windows.empty())
                {
                    selected_index_ = 0;
                }
                else
                {
                    // If we removed the selected window, clamp to valid range
                    selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(updated_windows.size()) - 1);
                }
                
                previous_selected_index_ = -1;
                rename_buffer_.fill('\0');
                
                ImGui::EndTable();
                return;
            }
        }

        ImGui::EndTable();
    }

    if (selected_index_ != previous_selected_index_)
    {
        previous_selected_index_ = selected_index_;
        rename_buffer_.fill('\0');
        UIWindow* current = windows[selected_index_];
        std::snprintf(rename_buffer_.data(), rename_buffer_.size(), "%s", current->displayName());
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(i18n::get("settings.rename_instance"));
    ImGui::InputText("##instance_rename", rename_buffer_.data(), rename_buffer_.size());
    ImGui::SameLine();
    if (ImGui::Button(i18n::get("apply")))
    {
        UIWindow* current = windows[selected_index_];
        current->rename(rename_buffer_.data());
        std::snprintf(rename_buffer_.data(), rename_buffer_.size(), "%s", current->displayName());
    }
}

void GlobalSettingsPanel::renderDQXClaritySection()
{
    if (!dqxc_launcher_)
        return;
    
    // Get current status
    DQXClarityStatus status = dqxc_launcher_->getStatus();
    std::string status_str = dqxc_launcher_->getStatusString();
    
    // Determine color based on status
    ImVec4 status_color;
    switch (status)
    {
        case DQXClarityStatus::Running:
            status_color = UITheme::successColor();
            break;
        case DQXClarityStatus::Connected:
            status_color = UITheme::successColor();
            break;
        case DQXClarityStatus::Disconnected:
            status_color = UITheme::errorColor();
            break;
        case DQXClarityStatus::Stopped:
            status_color = UITheme::disabledColor();
            break;
    }
    
    // Display concise status only
    ImGui::TextColored(status_color, "●");
    ImGui::SameLine();
    ImGui::TextUnformatted(i18n::get("settings.dqxc.status_label"));
    ImGui::SameLine();
    ImGui::TextColored(status_color, "%s", status_str.c_str());

#ifndef _WIN32
    // Show wineserver info on Linux
    if (status == DQXClarityStatus::Disconnected)
    {
        ImGui::Spacing();
        ImGui::TextColored(UITheme::errorColor(), "%s", i18n::get("common.warning"));
        ImGui::TextWrapped("%s", i18n::get("settings.dqxc.wineserver_mismatch"));
    }
#endif
}

void GlobalSettingsPanel::renderDebugSection()
{
    ImGui::TextUnformatted(i18n::get("settings.dqxc.debug_title"));

    if (dqxc_launcher_)
    {
        // Launch/Stop moved to Debug; guard against mid-stages
        auto stage = dqxc_launcher_->getEngineStage();
        bool is_busy = (stage != dqxclarity::Status::Stopped && stage != dqxclarity::Status::Hooked);
        bool is_stopped = (stage == dqxclarity::Status::Stopped);
        bool dqx_running = dqxc_launcher_->isDQXGameRunning();

        if (is_stopped)
        {
            bool disable = is_busy || !dqx_running;
            if (disable) ImGui::BeginDisabled();
            std::string label = std::string(i18n::get("common.start")) + "##dqxc_dbg";
            if (ImGui::Button(label.c_str(), ImVec2(120, 0)))
            {
                dqxc_launcher_->launch();
            }
            if (disable) {
                ImGui::EndDisabled();
                if (!dqx_running) { ImGui::SameLine(); ImGui::TextDisabled("%s", i18n::get("settings.dqxc.not_running_hint")); }
            }
        }
        else
        {
            bool disable = is_busy; // disable Stop during Starting/Stopping
            if (disable) ImGui::BeginDisabled();
            std::string label = std::string(i18n::get("common.stop")) + "##dqxc_dbg";
            if (ImGui::Button(label.c_str(), ImVec2(120, 0)))
            {
                dqxc_launcher_->stop();
            }
            if (disable) ImGui::EndDisabled();
        }
    }

    ImGui::SeparatorText(i18n::get("settings.logs"));

    // Refresh log content every 2 seconds
    float current_time = static_cast<float>(ImGui::GetTime());
    if (current_time - last_log_refresh_time_ > 2.0f)
    {
        // Read the app's own log from current working directory
        std::string log_path = "logs/run.log";
        cached_log_content_ = readLogFile(log_path);
        last_log_refresh_time_ = current_time;
    }
    
    // Display log content in a read-only text box
    ImGui::InputTextMultiline(
        "##dqxc_logs",
        const_cast<char*>(cached_log_content_.c_str()),
        cached_log_content_.size(),
        ImVec2(-1, 300),
        ImGuiInputTextFlags_ReadOnly
    );
    
}

std::string GlobalSettingsPanel::readLogFile(const std::string& path, size_t max_lines)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
return i18n::format("settings.log_viewer.not_found", {{"path", path}});
    }
    
    std::vector<std::string> lines;
    std::string line;
    
    // Read all lines
    while (std::getline(file, line))
    {
        lines.push_back(line);
    }
    
    // Keep only the last max_lines
    size_t start_idx = 0;
    if (lines.size() > max_lines)
    {
        start_idx = lines.size() - max_lines;
    }
    
    // Join lines
    std::ostringstream oss;
    for (size_t i = start_idx; i < lines.size(); ++i)
    {
        oss << lines[i];
        if (i < lines.size() - 1)
            oss << "\n";
    }
    
    return oss.str();
}

void GlobalSettingsPanel::renderStatusSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.status"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool dqx_running = ProcessDetector::isProcessRunning("DQXGame.exe");
        ImVec4 game_status_color = dqx_running ? UITheme::successColor() : UITheme::errorColor();
        const char* game_status_text = dqx_running ? i18n::get("settings.status.running") : i18n::get("settings.status.not_running");

        // DQX Game Status
        ImGui::TextColored(game_status_color, "●");
        ImGui::SameLine();
        ImGui::TextUnformatted(i18n::get("settings.status.game_label"));
        ImGui::SameLine();
        ImGui::TextColored(game_status_color, "%s", game_status_text);

#ifdef _WIN32
        // Locale Status (Windows only) - based on window title
        if (dqx_running)
        {
            ProcessLocale locale = ProcessLocaleChecker::checkProcessLocale("DQXGame.exe");
            ImVec4 locale_color;
            const char* locale_text;
            
            switch (locale)
            {
                case ProcessLocale::Japanese:
                    locale_color = UITheme::successColor();
                    locale_text = i18n::get("settings.status.japanese");
                    break;
                case ProcessLocale::NonJapanese:
                    locale_color = UITheme::warningColor();
                    locale_text = i18n::get("settings.status.non_japanese");
                    break;
                case ProcessLocale::Unknown:
                default:
                    locale_color = UITheme::disabledColor();
                    locale_text = i18n::get("settings.status.unknown");
                    break;
            }
            
            ImGui::TextColored(locale_color, "●");
            ImGui::SameLine();
            ImGui::TextUnformatted(i18n::get("settings.status.locale_label"));
            ImGui::SameLine();
            ImGui::TextColored(locale_color, "%s", locale_text);
        }
#endif

        renderDQXClaritySection();
    }
}

void GlobalSettingsPanel::renderWindowManagementSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.window_management")))
    {
        ImGui::TextUnformatted(i18n::get("settings.window_type"));
        renderTypeSelector();
        ImGui::Spacing();

        auto windows = registry_.windowsByType(selected_type_);
        renderInstanceSelector(windows);
    }
}

void GlobalSettingsPanel::renderAppearanceSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.appearance"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        float ui_scale = 1.0f;
        if (auto* cm = ConfigManager_Get())
            ui_scale = cm->getUIScale();
        ImGui::TextUnformatted(i18n::get("settings.ui_scale"));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderFloat("##ui_scale_slider", &ui_scale, 0.75f, 2.0f, "%.2fx"))
        {
            if (auto* cm = ConfigManager_Get()) cm->setUIScale(ui_scale);
        }

        // UI Language selector (proof of concept)
        ImGui::TextUnformatted(i18n::get("settings.ui_language.label"));
        const char* langs[] = {
            i18n::get("settings.ui_language.option_en"),
            i18n::get("settings.ui_language.option_zh_cn")
        };
        int idx = 0;
        if (auto* cm = ConfigManager_Get())
        {
            const char* code = cm->getUILanguageCode();
            if (code && std::string(code) == "zh-CN") idx = 1;
        }
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##ui_lang_combo", &idx, langs, IM_ARRAYSIZE(langs)))
        {
            const char* new_code = (idx == 1) ? "zh-CN" : "en";
            if (auto* cm = ConfigManager_Get()) cm->setUILanguageCode(new_code);
            i18n::set_language(new_code);
        }

        ImGui::TextUnformatted(i18n::get("settings.app_mode.label"));
        const char* app_modes[] = {
            i18n::get("settings.app_mode.items.normal"),
            i18n::get("settings.app_mode.items.borderless"),
            i18n::get("settings.app_mode.items.mini")
        };
        int app_mode_idx = 0;
        if (auto* cm = ConfigManager_Get()) app_mode_idx = static_cast<int>(cm->getAppMode());
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##app_mode_combo", &app_mode_idx, app_modes, IM_ARRAYSIZE(app_modes)))
        {
            if (auto* cm = ConfigManager_Get()) cm->setAppMode(static_cast<ConfigManager::AppMode>(app_mode_idx));
        }
        
    }
}
