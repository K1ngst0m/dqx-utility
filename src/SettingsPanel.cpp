#include "SettingsPanel.hpp"

#include "DialogWindow.hpp"
#include "ProcessDetector.hpp"
#include "config/ConfigManager.hpp"

#include <algorithm>
#include <imgui.h>
#include <cstdio>

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
{
}

// Renders the settings window with type/instance selectors.
void SettingsPanel::render(bool& open)
{
    if (!open)
        return;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Global Settings", &open, flags))
    {
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool dqx_running = ProcessDetector::isProcessRunning("DQXGame.exe");
            ImVec4 status_color = dqx_running ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            const char* status_symbol = dqx_running ? "●" : "●";
            const char* status_text = dqx_running ? "Running" : "Not Running";
            
            ImGui::TextColored(status_color, "%s", status_symbol);
            ImGui::SameLine();
            ImGui::TextUnformatted("DQXGame.exe:");
            ImGui::SameLine();
            ImGui::TextUnformatted(status_text);
        }
        
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

        if (ImGui::CollapsingHeader("Window Management", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Window Type");
            renderTypeSelector();
            ImGui::Separator();

            auto windows = registry_.windowsByType(selected_type_);
            renderInstanceSelector(windows);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
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
