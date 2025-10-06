#pragma once

#include <array>
#include <imgui.h>

struct UIState
{
    static constexpr std::size_t FontPathSize = 512;

    float width;
    float height;
    ImVec2 padding;
    float rounding;
    float border_thickness;
    float background_alpha;
    float font_size;
    float font_base_size;
    
    float vignette_thickness;

    std::array<char, FontPathSize> font_path{};
    ImVec2 window_pos;
    ImVec2 window_size;
    bool pending_reposition;
    bool pending_resize;
    bool has_custom_font;

    bool auto_scroll_to_new;

    ImFont* font;

    void applyDefaults()
    {
        width = 580.0f;
        height = 220.0f;
        padding = ImVec2(24.0f, 18.0f);
        rounding = 16.0f;
        border_thickness = 2.0f;
        background_alpha = 0.78f;
        font_size = 28.0f;
        font_base_size = 28.0f;
        
        vignette_thickness = 12.0f;
        
        font_path.fill('\0');
        window_pos = ImVec2(0.0f, 0.0f);
        window_size = ImVec2(width, height);
        pending_reposition = true;
        pending_resize = true;
        has_custom_font = false;

        auto_scroll_to_new = true;
        
        font = nullptr;
    }
};
