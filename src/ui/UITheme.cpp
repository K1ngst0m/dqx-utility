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

void UITheme::applyDockingTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4 bg = dialogBgColor();
    s.Colors[ImGuiCol_WindowBg] = bg;
    s.Colors[ImGuiCol_ChildBg] = ImVec4(bg.x, bg.y, bg.z, bg.w * 0.9f);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(bg.x, bg.y, bg.z, 0.95f);
    s.Colors[ImGuiCol_Border] = dialogBorderColor();
    s.Colors[ImGuiCol_Text] = dialogTextColor();
    s.Colors[ImGuiCol_Separator] = dialogSeparatorColor();
    s.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0.0f);
    s.Colors[ImGuiCol_DockingPreview] = ImVec4(1.0f, 0.87f, 0.13f, 0.35f);
    ImVec4 tab = ImVec4(bg.x, bg.y, bg.z, 0.85f);
    ImVec4 tab_h = ImVec4(bg.x + 0.12f, bg.y + 0.12f, bg.z + 0.12f, 0.95f);
    s.Colors[ImGuiCol_Tab] = tab;
    s.Colors[ImGuiCol_TabHovered] = tab_h;
    s.Colors[ImGuiCol_TabActive] = ImVec4(tab_h.x, tab_h.y, tab_h.z, 1.0f);
    s.Colors[ImGuiCol_TabUnfocused] = tab;
    s.Colors[ImGuiCol_TabUnfocusedActive] = tab;
    s.Colors[ImGuiCol_TitleBg] = tab;
    s.Colors[ImGuiCol_TitleBgActive] = tab_h;
    s.WindowRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.WindowBorderSize = 2.0f;
    s.TabBorderSize = 1.0f;
}
