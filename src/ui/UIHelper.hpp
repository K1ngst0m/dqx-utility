#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <string>

#include "Localization.hpp"
#include "UITheme.hpp"

namespace ui
{

inline std::string LocalizedOrFallback(const char* key, const char* fallback)
{
    const std::string& value = i18n::get_str(key);
    if (value.empty() || value == key)
    {
        return std::string(fallback);
    }
    return value;
}

inline void RenderVignette(const ImVec2& win_pos, const ImVec2& win_size, float thickness, float rounding,
                           float alpha_multiplier)
{
    thickness = std::max(0.0f, thickness);
    if (thickness <= 0.0f)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding0 = std::max(0.0f, rounding);

    int steps = static_cast<int>(std::ceil(thickness * 3.0f));
    steps = std::clamp(steps, 1, 256);

    float max_alpha = std::clamp(0.30f + 0.006f * thickness, 0.30f, 0.65f);

    for (int i = 0; i < steps; ++i)
    {
        float t = (steps <= 1) ? 0.0f : (static_cast<float>(i) / (steps - 1));
        float inset = t * thickness;
        ImVec2 pmin(win_pos.x + inset, win_pos.y + inset);
        ImVec2 pmax(win_pos.x + win_size.x - inset, win_pos.y + win_size.y - inset);
        float r = std::max(0.0f, rounding0 - inset);
        float a = max_alpha * (1.0f - t);
        a = a * a;
        a *= alpha_multiplier;
        if (a <= 0.0f)
            continue;
        ImU32 col = IM_COL32(0, 0, 0, static_cast<int>(a * 255.0f));
        dl->AddRect(pmin, pmax, col, r, 0, 1.0f);
    }
}

inline void RenderOutlinedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px,
                               float wrap_width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 text_col_v4 = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    text_col_v4.w *= ImGui::GetStyle().Alpha;
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(text_col_v4);
    ImU32 outline_col = IM_COL32(0, 0, 0, static_cast<int>(text_col_v4.w * 255.0f));

    float thickness = std::clamp(std::round(font_size_px * 0.06f), 1.0f, 3.0f);

    for (int ox = -1; ox <= 1; ++ox)
    {
        for (int oy = -1; oy <= 1; ++oy)
        {
            if (ox == 0 && oy == 0)
                continue;
            dl->AddText(font, font_size_px, ImVec2(position.x + ox * thickness, position.y + oy * thickness),
                        outline_col, text, nullptr, wrap_width);
        }
    }
    dl->AddText(font, font_size_px, position, text_col, text, nullptr, wrap_width);
}

inline void DrawFullWidthSeparator(float thickness, const ImVec4& color)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
    ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
    float x1 = win_pos.x + cr_min.x;
    float x2 = win_pos.x + cr_max.x;
    float y = ImGui::GetCursorScreenPos().y;

    ImVec4 col = color;
    col.w *= ImGui::GetStyle().Alpha;
    ImU32 col_u32 = ImGui::ColorConvertFloat4ToU32(col);
    draw_list->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + thickness), col_u32);
    ImGui::Dummy(ImVec2(0.0f, thickness));
}

inline void DrawDefaultSeparator()
{
    DrawFullWidthSeparator(UITheme::dialogSeparatorThickness(), UITheme::dialogSeparatorColor());
}

} // namespace ui

class MonsterManager;

namespace ui
{

using MonsterLinkHandler = std::function<void(const std::string&)>;

void SetMonsterLinkHandler(MonsterLinkHandler handler);
void RenderAnnotatedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px, float wrap_width, MonsterManager* monster_mgr = nullptr);
ImVec2 CalcAnnotatedTextSize(const char* text, ImFont* font, float font_size_px, float wrap_width);

} // namespace ui
