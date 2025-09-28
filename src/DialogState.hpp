#pragma once

#include <array>
#include <algorithm>
#include <vector>
#include <imgui.h>

struct DialogState
{
    static constexpr std::size_t TitleBufferSize = 128;
    static constexpr std::size_t BodyBufferSize  = 1024;
    static constexpr std::size_t FontPathSize    = 512;
    static constexpr std::size_t EntryBufferSize = 256;
    static constexpr std::size_t PortfilePathSize = 512;

    float width            = 580.0f;
    float height           = 220.0f;
    ImVec2 padding         = ImVec2(24.0f, 18.0f);
    float rounding         = 16.0f;
    float border_thickness = 2.0f;
    float background_alpha = 0.78f;
    float font_size         = 28.0f;
    float font_base_size    = 28.0f;

    std::array<char, FontPathSize>    font_path{};
    std::vector<std::array<char, EntryBufferSize>> segments;
    std::array<char, EntryBufferSize> append_buffer{};
    int editing_index = -1;
    std::array<char, BodyBufferSize> edit_buffer{};

    ImVec2 window_pos      = ImVec2(0.0f, 0.0f);
    ImVec2 window_size     = ImVec2(width, height);
    bool pending_reposition = true;
    bool pending_resize     = true;
    bool has_custom_font    = false;

    std::array<char, PortfilePathSize> portfile_path{};
    bool auto_scroll_to_new = true;

    ImFont* font = nullptr;
};
