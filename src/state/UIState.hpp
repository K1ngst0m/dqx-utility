#pragma once

#include <array>
#include <imgui.h>

struct UIState
{
    static constexpr std::size_t FontPathSize = 512;

    float width            = 580.0f;
    float height           = 220.0f;
    ImVec2 padding         = ImVec2(24.0f, 18.0f);
    float rounding         = 16.0f;
    float border_thickness = 2.0f;
    float background_alpha = 0.78f;
    float font_size        = 28.0f;
    float font_base_size   = 28.0f;

    std::array<char, FontPathSize> font_path{};
    ImVec2 window_pos      = ImVec2(0.0f, 0.0f);
    ImVec2 window_size     = ImVec2(width, height);
    bool pending_reposition = true;
    bool pending_resize     = true;
    bool has_custom_font    = false;

    ImFont* font = nullptr;
};