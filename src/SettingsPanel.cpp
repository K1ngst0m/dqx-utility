#include "SettingsPanel.hpp"

#include "DialogWindow.hpp"
#include "FontManager.hpp"

#include <imgui.h>

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
SettingsPanel::SettingsPanel(WindowRegistry& registry, FontManager& font_manager, ImGuiIO& io)
    : registry_(registry), io_(io)
{
    (void)font_manager;
}

// Renders the settings window with type/instance selectors.
void SettingsPanel::render()
{
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
    if (ImGui::Begin("Window Settings", nullptr, flags))
    {
        ImGui::TextUnformatted("Window Type");
        renderTypeSelector();
        ImGui::Separator();

        auto windows = registry_.windowsByType(selected_type_);
        renderInstanceSelector(windows);
        ImGui::Separator();

        if (!windows.empty() && selected_index_ >= 0 && selected_index_ < static_cast<int>(windows.size()))
        {
            windows[selected_index_]->renderSettings(io_);
        }
        else
        {
            ImGui::TextDisabled("No instance selected.");
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
            selected_index_ = static_cast<int>(registry_.windowsByType(UIWindowType::Dialog).size()) - 1;
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

    std::vector<const char*> labels;
    labels.reserve(windows.size());
    for (UIWindow* window : windows)
        labels.push_back(window->label());

    if (selected_index_ >= static_cast<int>(labels.size()))
        selected_index_ = static_cast<int>(labels.size()) - 1;

    ImGui::Spacing();
    ImGui::TextUnformatted("Instance");
    if (ImGui::BeginCombo("##window_instance_combo", labels[selected_index_]))
    {
        for (int i = 0; i < static_cast<int>(labels.size()); ++i)
        {
            const bool selected = (i == selected_index_);
            if (ImGui::Selectable(labels[i], selected))
                selected_index_ = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
}
