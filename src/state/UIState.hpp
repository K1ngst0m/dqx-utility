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
    bool border_enabled;
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
    bool is_docked;  // Cached docked state for context menu

    // Auto-fade settings (per-dialog)
    bool fade_enabled;
    float fade_timeout;  // seconds
    
    // Auto-fade state
    float last_activity_time;   // Time since last text append or mouse hover
    float current_alpha_multiplier;  // Current fade multiplier (1.0 = fully visible, 0.0 = fully hidden)

    ImFont* font;

    void applyDefaults()
    {
        width = 580.0f;
        height = 220.0f;
        padding = ImVec2(24.0f, 18.0f);
        rounding = 16.0f;
        border_thickness = 2.0f;
        border_enabled = true;
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
        is_docked = false;
        
        fade_enabled = false;
        fade_timeout = 20.0f;
        last_activity_time = 0.0f;
        current_alpha_multiplier = 1.0f;
        
        font = nullptr;
    }
};
