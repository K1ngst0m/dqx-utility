#include "UIHelper.hpp"
#include "EntityAnnotation.hpp"
#include "../monster/MonsterManager.hpp"
#include <imgui.h>
#include <plog/Log.h>

namespace ui
{

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
    
    ImVec2 cursor = position;
    
    for (const auto& span : spans)
    {
        if (span.type == entity::SpanType::Plain)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                for (int oy = -1; oy <= 1; ++oy)
                {
                    if (ox == 0 && oy == 0)
                        continue;
                    dl->AddText(font, font_size_px, ImVec2(cursor.x + ox * thickness, cursor.y + oy * thickness),
                                outline_col, span.text.c_str(), nullptr, wrap_width);
                }
            }
            dl->AddText(font, font_size_px, cursor, text_col, span.text.c_str(), nullptr, wrap_width);
            
            ImVec2 text_size = font->CalcTextSizeA(font_size_px, FLT_MAX, wrap_width, span.text.c_str());
            cursor.x += text_size.x;
        }
        else if (span.type == entity::SpanType::MonsterLink)
        {
            ImVec2 text_size = font->CalcTextSizeA(font_size_px, FLT_MAX, wrap_width, span.text.c_str());
            ImVec2 link_min = cursor;
            ImVec2 link_max = ImVec2(cursor.x + text_size.x, cursor.y + text_size.y);
            
            bool hovered = ImGui::IsMouseHoveringRect(link_min, link_max);
            ImU32 current_link_col = hovered ? link_hover_col : link_col;
            
            for (int ox = -1; ox <= 1; ++ox)
            {
                for (int oy = -1; oy <= 1; ++oy)
                {
                    if (ox == 0 && oy == 0)
                        continue;
                    dl->AddText(font, font_size_px, ImVec2(cursor.x + ox * thickness, cursor.y + oy * thickness),
                                outline_col, span.text.c_str(), nullptr, wrap_width);
                }
            }
            dl->AddText(font, font_size_px, cursor, current_link_col, span.text.c_str(), nullptr, wrap_width);
            
            float underline_y = cursor.y + text_size.y - 1.0f;
            dl->AddLine(ImVec2(link_min.x, underline_y), ImVec2(link_max.x, underline_y), current_link_col, 1.0f);
            
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (monster_mgr)
                    {
                        auto monster_info = monster_mgr->findMonsterById(span.entity_id);
                        if (monster_info.has_value())
                        {
                            PLOG_INFO << "=== Monster Info: " << monster_info->name << " ===";
                            PLOG_INFO << "ID: " << monster_info->id;
                            PLOG_INFO << "Category: " << monster_info->category;
                            
                            PLOG_INFO << "Stats:";
                            if (monster_info->stats.hp.has_value())
                                PLOG_INFO << "  HP: " << monster_info->stats.hp.value();
                            if (monster_info->stats.mp.has_value())
                                PLOG_INFO << "  MP: " << monster_info->stats.mp.value();
                            if (monster_info->stats.attack.has_value())
                                PLOG_INFO << "  Attack: " << monster_info->stats.attack.value();
                            if (monster_info->stats.defense.has_value())
                                PLOG_INFO << "  Defense: " << monster_info->stats.defense.value();
                            if (monster_info->stats.exp.has_value())
                                PLOG_INFO << "  EXP: " << monster_info->stats.exp.value();
                            if (monster_info->stats.gold.has_value())
                                PLOG_INFO << "  Gold: " << monster_info->stats.gold.value();
                            if (monster_info->stats.training.has_value())
                                PLOG_INFO << "  Training: " << monster_info->stats.training.value();
                            if (monster_info->stats.weak_level.has_value())
                                PLOG_INFO << "  Weak Level: " << monster_info->stats.weak_level.value();
                            if (monster_info->stats.crystal_level.has_value() && !monster_info->stats.crystal_level.value().empty())
                                PLOG_INFO << "  Crystal Level: " << monster_info->stats.crystal_level.value();
                            
                            PLOG_INFO << "Resistances:";
                            if (monster_info->resistances.fire.has_value())
                                PLOG_INFO << "  Fire: " << monster_info->resistances.fire.value();
                            if (monster_info->resistances.ice.has_value())
                                PLOG_INFO << "  Ice: " << monster_info->resistances.ice.value();
                            if (monster_info->resistances.wind.has_value())
                                PLOG_INFO << "  Wind: " << monster_info->resistances.wind.value();
                            if (monster_info->resistances.thunder.has_value())
                                PLOG_INFO << "  Thunder: " << monster_info->resistances.thunder.value();
                            if (monster_info->resistances.earth.has_value())
                                PLOG_INFO << "  Earth: " << monster_info->resistances.earth.value();
                            if (monster_info->resistances.dark.has_value())
                                PLOG_INFO << "  Dark: " << monster_info->resistances.dark.value();
                            if (monster_info->resistances.light.has_value())
                                PLOG_INFO << "  Light: " << monster_info->resistances.light.value();
                            
                            if (!monster_info->locations.empty())
                            {
                                PLOG_INFO << "Locations (" << monster_info->locations.size() << "):";
                                for (const auto& loc : monster_info->locations)
                                {
                                    if (loc.notes.has_value() && !loc.notes.value().empty())
                                        PLOG_INFO << "  - " << loc.area << " (" << loc.notes.value() << ")";
                                    else
                                        PLOG_INFO << "  - " << loc.area;
                                }
                            }
                            
                            if (!monster_info->drops.normal.empty())
                            {
                                PLOG_INFO << "Normal Drops:";
                                for (const auto& drop : monster_info->drops.normal)
                                    PLOG_INFO << "  - " << drop;
                            }
                            
                            if (!monster_info->drops.rare.empty())
                            {
                                PLOG_INFO << "Rare Drops:";
                                for (const auto& drop : monster_info->drops.rare)
                                    PLOG_INFO << "  - " << drop;
                            }
                            
                            if (!monster_info->drops.orbs.empty())
                            {
                                PLOG_INFO << "Orbs:";
                                for (const auto& orb : monster_info->drops.orbs)
                                {
                                    if (!orb.effect.empty())
                                        PLOG_INFO << "  - [" << orb.orb_type << "] " << orb.effect;
                                    else
                                        PLOG_INFO << "  - [" << orb.orb_type << "]";
                                }
                            }
                            
                            if (!monster_info->drops.white_treasure.empty())
                            {
                                PLOG_INFO << "White Treasure:";
                                for (const auto& treasure : monster_info->drops.white_treasure)
                                    PLOG_INFO << "  - " << treasure;
                            }
                            
                            if (!monster_info->source_url.empty())
                                PLOG_INFO << "Source URL: " << monster_info->source_url;
                            
                            PLOG_INFO << "==========================================";
                        }
                    }
                }
            }
            
            cursor.x += text_size.x;
        }
    }
}

} // namespace ui
