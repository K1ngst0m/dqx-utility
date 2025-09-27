#pragma once

#include <imgui.h>
#include <algorithm>

inline void DrawMenuIcon(ImDrawList* draw_list, const ImVec2& center, float radius, float visibility, bool hovered)
{
    const float alpha = std::clamp(visibility, 0.0f, 1.0f);
    const ImU32 border_col = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, alpha));
    const ImU32 fill_top   = ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.85f, alpha));
    const ImU32 fill_bot   = ImGui::GetColorU32(ImVec4(0.65f, 0.65f, 0.65f, alpha));
    const ImU32 glow_col   = ImGui::GetColorU32(ImVec4(hovered ? 1.0f : 0.0f, hovered ? 0.8f : 0.0f, 0.0f, hovered ? 0.2f * alpha : 0.0f));

    draw_list->AddCircleFilled(center, radius + 2.0f, glow_col, 48);
    draw_list->AddCircleFilled(center, radius, fill_bot, 48);
    draw_list->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.2f), radius * 0.85f, fill_top, 48);
    draw_list->AddCircle(center, radius, border_col, 48, 2.0f);

    const float bar_height = radius * 0.25f;
    const float bar_spacing = bar_height * 0.65f;
    const float bar_width = radius * 1.2f;
    const float corner = bar_height * 0.45f;
    ImU32 bar_fill = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
    ImU32 bar_border = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, alpha));

    for (int i = -1; i <= 1; ++i)
    {
        float y = center.y + (bar_height + bar_spacing) * i;
        ImVec2 rect_min(center.x - bar_width * 0.5f, y - bar_height * 0.5f);
        ImVec2 rect_max(center.x + bar_width * 0.5f, y + bar_height * 0.5f);
        draw_list->AddRectFilled(rect_min, rect_max, bar_fill, corner);
        draw_list->AddRect(rect_min, rect_max, bar_border, corner, 0, 1.6f);
    }
}
