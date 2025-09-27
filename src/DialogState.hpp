#pragma once

#include <array>
#include <algorithm>
#include <imgui.h>

// DialogState stores per-instance dialog data and layout properties.
struct DialogState
{
    static constexpr std::size_t TitleBufferSize = 128;
    static constexpr std::size_t BodyBufferSize  = 1024;
    static constexpr std::size_t FontPathSize    = 512;

    float width            = 580.0f;
    float height           = 220.0f;
    float vertical_ratio   = 0.75f;
    ImVec2 padding         = ImVec2(24.0f, 18.0f);
    float rounding         = 16.0f;
    float border_thickness = 2.0f;
    bool show_title        = true;

    std::array<char, TitleBufferSize> title{};
    std::array<char, BodyBufferSize>  body{};
    std::array<char, FontPathSize>    font_path{};

    ImVec2 window_pos      = ImVec2(0.0f, 0.0f);
    ImVec2 window_size     = ImVec2(width, height);
    bool pending_reposition = true;
    bool pending_resize     = true;
    bool has_custom_font    = false;

    ImFont* font = nullptr;
};
