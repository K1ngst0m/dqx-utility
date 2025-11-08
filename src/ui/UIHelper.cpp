#include "UIHelper.hpp"
#include "EntityAnnotation.hpp"
#include "../monster/MonsterManager.hpp"
#include <imgui.h>
#include <plog/Log.h>

namespace ui
{

static MonsterLinkHandler g_monster_link_handler = nullptr;

void SetMonsterLinkHandler(MonsterLinkHandler handler)
{
    g_monster_link_handler = std::move(handler);
}

void RenderAnnotatedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px, float wrap_width, MonsterManager* monster_mgr)
{
    if (!text || !text[0])
        return;
    
    auto spans = entity::parseAnnotatedText(text);
    if (spans.empty())
        return;
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 text_col_v4 = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    text_col_v4.w *= ImGui::GetStyle().Alpha;
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(text_col_v4);
    ImU32 outline_col = IM_COL32(0, 0, 0, static_cast<int>(text_col_v4.w * 255.0f));
    
    ImVec4 link_col_v4 = ImVec4(0.4f, 0.7f, 1.0f, text_col_v4.w);
    ImU32 link_col = ImGui::ColorConvertFloat4ToU32(link_col_v4);
    ImU32 link_hover_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.85f, 1.0f, text_col_v4.w));
    
    float thickness = std::clamp(std::round(font_size_px * 0.06f), 1.0f, 3.0f);
    float line_height = font->CalcTextSizeA(font_size_px, FLT_MAX, 0.0f, "A").y;
    
    ImVec2 cursor = position;
    float line_start_x = position.x;
    
    for (const auto& span : spans)
    {
        if (span.type == entity::SpanType::Plain)
        {
            const char* text_begin = span.text.c_str();
            const char* text_end = text_begin + span.text.length();
            
            float remaining_width = (wrap_width > 0.0f) ? (line_start_x + wrap_width - cursor.x) : 0.0f;
            
            if (wrap_width > 0.0f && cursor.x > line_start_x)
            {
                ImVec2 check_size = font->CalcTextSizeA(font_size_px, FLT_MAX, remaining_width, text_begin, text_end);
                if (check_size.y > font_size_px)
                {
                    cursor.x = line_start_x;
                    cursor.y += line_height;
                    remaining_width = wrap_width;
                }
            }
            
            ImVec2 text_size = font->CalcTextSizeA(font_size_px, FLT_MAX, remaining_width, text_begin, text_end);
            
            for (int ox = -1; ox <= 1; ++ox)
            {
                for (int oy = -1; oy <= 1; ++oy)
                {
                    if (ox == 0 && oy == 0)
                        continue;
                    dl->AddText(font, font_size_px, ImVec2(cursor.x + ox * thickness, cursor.y + oy * thickness),
                                outline_col, text_begin, text_end, remaining_width);
                }
            }
            dl->AddText(font, font_size_px, cursor, text_col, text_begin, text_end, remaining_width);
            
            if (text_size.y > font_size_px)
            {
                cursor.x = line_start_x + text_size.x;
            }
            else
            {
                cursor.x += text_size.x;
            }
            cursor.y += text_size.y - font_size_px;
        }
        else if (span.type == entity::SpanType::MonsterLink)
        {
            const char* text_begin = span.text.c_str();
            const char* text_end = text_begin + span.text.length();
            
            ImVec2 link_size = font->CalcTextSizeA(font_size_px, FLT_MAX, 0.0f, text_begin, text_end);
            
            float available_width = (wrap_width > 0.0f) ? (line_start_x + wrap_width - cursor.x) : FLT_MAX;
            if (wrap_width > 0.0f && link_size.x > available_width && cursor.x > line_start_x)
            {
                cursor.x = line_start_x;
                cursor.y += line_height;
            }
            
            ImVec2 link_min = cursor;
            ImVec2 link_max = ImVec2(cursor.x + link_size.x, cursor.y + link_size.y);
            
            bool hovered = ImGui::IsMouseHoveringRect(link_min, link_max);
            ImU32 current_link_col = hovered ? link_hover_col : link_col;
            
            for (int ox = -1; ox <= 1; ++ox)
            {
                for (int oy = -1; oy <= 1; ++oy)
                {
                    if (ox == 0 && oy == 0)
                        continue;
                    dl->AddText(font, font_size_px, ImVec2(cursor.x + ox * thickness, cursor.y + oy * thickness),
                                outline_col, text_begin, text_end);
                }
            }
            dl->AddText(font, font_size_px, cursor, current_link_col, text_begin, text_end);
            
            float underline_y = cursor.y + link_size.y - 1.0f;
            dl->AddLine(ImVec2(link_min.x, underline_y), ImVec2(link_max.x, underline_y), current_link_col, 1.0f);
            
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (g_monster_link_handler)
                    {
                        g_monster_link_handler(span.entity_id);
                    }
                    else if (monster_mgr)
                    {
                        auto monster_info = monster_mgr->findMonsterById(span.entity_id);
                        if (monster_info.has_value())
                        {
                            PLOG_INFO << "=== Monster Info: " << monster_info->name << " ===";
                            PLOG_INFO << "ID: " << monster_info->id;
                            PLOG_INFO << "Category: " << monster_info->category;
                            PLOG_INFO << "==========================================";
                        }
                    }
                }
            }
            
            cursor.x += link_size.x;
        }
    }
}

ImVec2 CalcAnnotatedTextSize(const char* text, ImFont* font, float font_size_px, float wrap_width)
{
    if (!text || !text[0])
        return ImVec2(0.0f, 0.0f);

    auto spans = entity::parseAnnotatedText(text);
    if (spans.empty())
        return ImVec2(0.0f, 0.0f);

    float line_height = font->CalcTextSizeA(font_size_px, FLT_MAX, 0.0f, "A").y;
    float line_start_x = 0.0f;

    ImVec2 cursor(line_start_x, 0.0f);
    float max_width = 0.0f;
    float total_height = 0.0f;

    for (const auto& span : spans)
    {
        if (span.type == entity::SpanType::Plain)
        {
            const char* text_begin = span.text.c_str();
            const char* text_end = text_begin + span.text.length();
            
            float remaining_width = (wrap_width > 0.0f) ? (line_start_x + wrap_width - cursor.x) : 0.0f;
            
            if (wrap_width > 0.0f && cursor.x > line_start_x)
            {
                ImVec2 check_size = font->CalcTextSizeA(font_size_px, FLT_MAX, remaining_width, text_begin, text_end);
                if (check_size.y > font_size_px)
                {
                    cursor.x = line_start_x;
                    cursor.y += line_height;
                    remaining_width = wrap_width;
                }
            }
            
            ImVec2 text_size = font->CalcTextSizeA(font_size_px, FLT_MAX, remaining_width, text_begin, text_end);
            
            if (text_size.y > font_size_px)
            {
                max_width = std::max(max_width, line_start_x + text_size.x);
                cursor.x = line_start_x + text_size.x;
            }
            else
            {
                max_width = std::max(max_width, cursor.x + text_size.x);
                cursor.x += text_size.x;
            }
            cursor.y += text_size.y - font_size_px;
            total_height = std::max(total_height, cursor.y + font_size_px);
        }
        else if (span.type == entity::SpanType::MonsterLink)
        {
            const char* text_begin = span.text.c_str();
            const char* text_end = text_begin + span.text.length();
            
            ImVec2 link_size = font->CalcTextSizeA(font_size_px, FLT_MAX, 0.0f, text_begin, text_end);
            
            float available_width = (wrap_width > 0.0f) ? (line_start_x + wrap_width - cursor.x) : FLT_MAX;
            if (wrap_width > 0.0f && link_size.x > available_width && cursor.x > line_start_x)
            {
                cursor.x = line_start_x;
                cursor.y += line_height;
            }
            
            max_width = std::max(max_width, cursor.x + link_size.x);
            cursor.x += link_size.x;
            total_height = std::max(total_height, cursor.y + link_size.y);
        }
    }

    return ImVec2(max_width, total_height);
}

} // namespace ui
