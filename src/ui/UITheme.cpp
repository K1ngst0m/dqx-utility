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

void UITheme::pushDialogStyle(float background_alpha, const ImVec2& padding, float rounding, float border_thickness,
                              bool border_enabled)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border_enabled ? border_thickness : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);

    ImVec4 dialog_bg = dialogBgColor();
    dialog_bg.w = background_alpha;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, dialog_bg);

    ImVec4 border_color = dialogBorderColor();
    ImGui::PushStyleColor(ImGuiCol_Border, border_color);

    ImGui::PushStyleColor(ImGuiCol_Text, dialogTextColor());
}

void UITheme::popDialogStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}

ImVec4 UITheme::statusColor(bool is_success, bool is_error, bool is_disabled)
{
    if (is_disabled)
        return disabled_;
    if (is_error)
        return error_;
    if (is_success)
        return success_;
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
    
    // Brown theme colors
    ImVec4 brown_base = ImVec4(0.325f, 0.224f, 0.161f, 1.0f);      // #533929
    ImVec4 brown_hovered = ImVec4(0.420f, 0.290f, 0.209f, 1.0f);   // 30% lighter
    ImVec4 brown_active = ImVec4(0.490f, 0.340f, 0.245f, 1.0f);    // 50% lighter
    ImVec4 brown_frame = ImVec4(0.325f, 0.224f, 0.161f, 0.54f);    // Transparent base
    ImVec4 brown_frame_hovered = ImVec4(0.420f, 0.290f, 0.209f, 0.67f);
    ImVec4 brown_frame_active = ImVec4(0.490f, 0.340f, 0.245f, 0.67f);
    
    // Collapsing headers
    s.Colors[ImGuiCol_Header] = brown_base;
    s.Colors[ImGuiCol_HeaderHovered] = brown_hovered;
    s.Colors[ImGuiCol_HeaderActive] = brown_active;
    
    // Frames (inputs, sliders, etc.)
    s.Colors[ImGuiCol_FrameBg] = brown_frame;
    s.Colors[ImGuiCol_FrameBgHovered] = brown_frame_hovered;
    s.Colors[ImGuiCol_FrameBgActive] = brown_frame_active;
    
    // Buttons
    s.Colors[ImGuiCol_Button] = brown_base;
    s.Colors[ImGuiCol_ButtonHovered] = brown_hovered;
    s.Colors[ImGuiCol_ButtonActive] = brown_active;
    
    // Slider/Scrollbar grabs
    s.Colors[ImGuiCol_SliderGrab] = ImVec4(0.560f, 0.390f, 0.280f, 1.0f);  // Lighter for visibility
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.650f, 0.450f, 0.325f, 1.0f);
    s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.490f, 0.340f, 0.245f, 0.51f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.560f, 0.390f, 0.280f, 0.67f);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.650f, 0.450f, 0.325f, 0.91f);
    
    // Checkmark
    s.Colors[ImGuiCol_CheckMark] = ImVec4(0.740f, 0.520f, 0.375f, 1.0f);  // Bright brown for visibility
    
    // Resize grip
    s.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.490f, 0.340f, 0.245f, 0.25f);
    s.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.560f, 0.390f, 0.280f, 0.67f);
    s.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.650f, 0.450f, 0.325f, 0.95f);
    
    // Title bar (for docked windows)
    s.Colors[ImGuiCol_TitleBg] = ImVec4(bg.x, bg.y, bg.z, 0.85f);
    s.Colors[ImGuiCol_TitleBgActive] = ImVec4(bg.x + 0.12f, bg.y + 0.12f, bg.z + 0.12f, 0.95f);
    s.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(bg.x, bg.y, bg.z, 0.51f);
    
    // Tabs
    ImVec4 tab = ImVec4(bg.x, bg.y, bg.z, 0.85f);
    ImVec4 tab_h = ImVec4(bg.x + 0.12f, bg.y + 0.12f, bg.z + 0.12f, 0.95f);
    s.Colors[ImGuiCol_Tab] = tab;
    s.Colors[ImGuiCol_TabHovered] = tab_h;
    s.Colors[ImGuiCol_TabActive] = ImVec4(tab_h.x, tab_h.y, tab_h.z, 1.0f);
    s.Colors[ImGuiCol_TabUnfocused] = tab;
    s.Colors[ImGuiCol_TabUnfocusedActive] = tab;
    
    // Text selection
    s.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.490f, 0.340f, 0.245f, 0.43f);
    
    // Style settings
    s.WindowRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.WindowBorderSize = 2.0f;
    s.TabBorderSize = 1.0f;
    s.ScrollbarSize = 16.0f;
    s.ScrollbarRounding = 9.0f;
    s.GrabRounding = 8.0f;
    s.GrabMinSize = 12.0f;
}
