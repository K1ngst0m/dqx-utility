#include "UITheme.hpp"

void UITheme::pushSettingsWindowStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
}

void UITheme::popSettingsWindowStyle()
{
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
}

void UITheme::pushDialogStyle(float background_alpha, const ImVec2& padding, float rounding, float border_thickness)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_thickness);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);

    ImVec4 dialog_bg = dialogBgColor();
    dialog_bg.w = background_alpha;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, dialog_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, dialogBorderColor());
    ImGui::PushStyleColor(ImGuiCol_Text, dialogTextColor());
}

void UITheme::popDialogStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}

ImVec4 UITheme::statusColor(bool is_success, bool is_error, bool is_disabled)
{
    if (is_disabled) return disabled_;
    if (is_error) return error_;
    if (is_success) return success_;
    return caution_;
}