#include "SettingsPanel.hpp"

#include "DialogWindow.hpp"
#include "ProcessDetector.hpp"
#include "DQXClarityLauncher.hpp"
#include "config/ConfigManager.hpp"
#include "UITheme.hpp"
#include "DQXClarityService.hpp"
#include "dqxclarity/api/dqxclarity.hpp"

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
SettingsPanel::SettingsPanel(WindowRegistry& registry)
    : registry_(registry)
    , dqxc_launcher_(std::make_unique<DQXClarityLauncher>())
{
    // Expose launcher globally for UI windows to fetch dialog messages
    DQXClarityService_Set(dqxc_launcher_.get());
}

// Renders the settings window with type/instance selectors.
void SettingsPanel::render(bool& open)
{
    if (!open)
        return;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 480.0f), ImGuiCond_FirstUseEver);
    UITheme::pushSettingsWindowStyle();

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Global Settings", &open, flags))
    {
        renderStatusSection();

        renderWindowManagementSection();

        renderAppearanceSection();

        
        if (ImGui::CollapsingHeader("Debug"))
        {
            renderDebugSection();
        }
    }
    ImGui::End();

    UITheme::popSettingsWindowStyle();
}

// Provides a combo box for selecting the active window type.
void SettingsPanel::renderTypeSelector()
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

    const char* preview = kWindowTypes[current_index].label;
    if (ImGui::BeginCombo("##window_type_combo", preview))
    {
        for (int i = 0; i < static_cast<int>(std::size(kWindowTypes)); ++i)
        {
            const bool selected = (i == current_index);
            if (ImGui::Selectable(kWindowTypes[i].label, selected))
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
void SettingsPanel::renderInstanceSelector(const std::vector<UIWindow*>& windows)
{
    if (selected_type_ == UIWindowType::Dialog)
    {
        if (ImGui::Button("Add Dialog"))
        {
            registry_.createDialogWindow();
            auto filtered = registry_.windowsByType(UIWindowType::Dialog);
            selected_index_ = static_cast<int>(filtered.size()) - 1;
            previous_selected_index_ = -1;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Total: %zu", windows.size());
    }

    if (windows.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("No instances available.");
        return;
    }

    if (selected_index_ >= static_cast<int>(windows.size()))
        selected_index_ = static_cast<int>(windows.size()) - 1;

    if (ImGui::BeginTable("InstanceTable", 3, ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
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
            ImGui::TextUnformatted(kWindowTypes[static_cast<int>(selected_type_)].label);

            ImGui::TableSetColumnIndex(2);
            std::string remove_id = std::string("Remove##") + win->windowLabel();
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
    ImGui::TextUnformatted("Rename Instance");
    ImGui::InputText("##instance_rename", rename_buffer_.data(), rename_buffer_.size());
    ImGui::SameLine();
    if (ImGui::Button("Apply"))
    {
        UIWindow* current = windows[selected_index_];
        current->rename(rename_buffer_.data());
        std::snprintf(rename_buffer_.data(), rename_buffer_.size(), "%s", current->displayName());
    }
}

void SettingsPanel::renderDQXClaritySection()
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
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImGui::TextColored(status_color, "%s", status_str.c_str());

#ifndef _WIN32
    // Show wineserver info on Linux
    if (status == DQXClarityStatus::Disconnected)
    {
        ImGui::Spacing();
        ImGui::TextColored(UITheme::errorColor(), "Warning:");
        ImGui::TextWrapped("dqxclarity is not on the same wineserver as DQXGame.exe. "
                           "Hooks will not work correctly. Try stopping and relaunching.");
    }
#endif
}

void SettingsPanel::renderDebugSection()
{
    ImGui::TextUnformatted("DQXClarity Debug");

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
            if (ImGui::Button("Start##dqxc_dbg", ImVec2(120, 0)))
            {
                dqxc_launcher_->launch();
            }
            if (disable) {
                ImGui::EndDisabled();
                if (!dqx_running) { ImGui::SameLine(); ImGui::TextDisabled("(DQXGame.exe not running)"); }
            }
        }
        else
        {
            bool disable = is_busy; // disable Stop during Starting/Stopping
            if (disable) ImGui::BeginDisabled();
            if (ImGui::Button("Stop##dqxc_dbg", ImVec2(120, 0)))
            {
                dqxc_launcher_->stop();
            }
            if (disable) ImGui::EndDisabled();
        }
    }

    ImGui::SeparatorText("Logs");

    // Refresh log content every 2 seconds
    float current_time = ImGui::GetTime();
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
    
    if (ImGui::Button("Refresh##logs"))
    {
        last_log_refresh_time_ = 0.0f;  // Force refresh
    }
}

std::string SettingsPanel::readLogFile(const std::string& path, size_t max_lines)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return "[Log file not found: " + path + "]";
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

void SettingsPanel::renderStatusSection()
{
    if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool dqx_running = ProcessDetector::isProcessRunning("DQXGame.exe");
        ImVec4 status_color = UITheme::statusColor(dqx_running, !dqx_running);
        const char* status_symbol = dqx_running ? "●" : "●";
        const char* status_text = dqx_running ? "Running" : "Not Running";

        ImGui::TextColored(status_color, "%s", status_symbol);
        ImGui::SameLine();
        ImGui::TextUnformatted("DQX Game:");
        ImGui::SameLine();
        ImGui::TextUnformatted(status_text);
        renderDQXClaritySection();
    }
}

void SettingsPanel::renderWindowManagementSection()
{
    if (ImGui::CollapsingHeader("Window Management", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextUnformatted("Window Type");
        renderTypeSelector();
        ImGui::Spacing();

        auto windows = registry_.windowsByType(selected_type_);
        renderInstanceSelector(windows);
    }
}

void SettingsPanel::renderAppearanceSection()
{
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float ui_scale = 1.0f;
        if (auto* cm = ConfigManager_Get())
            ui_scale = cm->getUIScale();
        ImGui::TextUnformatted("UI Scale");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderFloat("##ui_scale_slider", &ui_scale, 0.75f, 2.0f, "%.2fx"))
        {
            if (auto* cm = ConfigManager_Get()) cm->setUIScale(ui_scale);
        }
    }
}
