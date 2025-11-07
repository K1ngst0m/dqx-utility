#include "MonsterWindow.hpp"
#include "../FontManager.hpp"
#include "../GlobalStateManager.hpp"
#include "../UIHelper.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../monster/MonsterManager.hpp"
#include "../../monster/MonsterInfo.hpp"

#include <imgui.h>
#include <plog/Log.h>

MonsterWindow::MonsterWindow(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config,
                             MonsterManager& monster_manager, const std::string& monster_id, const std::string& name)
    : font_manager_(font_manager)
    , global_state_(global_state)
    , config_(config)
    , monster_manager_(monster_manager)
    , monster_id_(monster_id)
    , name_(name)
    , window_label_(name + "##Monster_" + monster_id)
{
}

MonsterWindow::~MonsterWindow() = default;

void MonsterWindow::rename(const char* new_name)
{
    if (!new_name)
        return;
    name_ = new_name;
    window_label_ = std::string(new_name) + "##Monster_" + monster_id_;
}

void MonsterWindow::render()
{
    if (want_focus_)
    {
        ImGui::SetNextWindowFocus();
        want_focus_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin(window_label_.c_str(), nullptr, ImGuiWindowFlags_None))
    {
        ImGui::End();
        return;
    }

    auto monster_info = monster_manager_.findMonsterById(monster_id_);
    
    if (!monster_info.has_value())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Monster not found (ID: %s)", monster_id_.c_str());
        ImGui::End();
        return;
    }

    // Title
    ImGui::TextWrapped("%s", monster_info->name.c_str());
    ImGui::TextDisabled("Category: %s", monster_info->category.c_str());
    
    ui::DrawDefaultSeparator();

    // Stats Section
    if (ImGui::CollapsingHeader((std::string("Stats##") + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderStatsSection(monster_info->stats);
    }

    // Resistances Section
    if (ImGui::CollapsingHeader((std::string("Resistances##") + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderResistancesSection(monster_info->resistances);
    }

    // Locations Section
    if (!monster_info->locations.empty())
    {
        if (ImGui::CollapsingHeader((std::string("Locations##") + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            renderLocationsSection(monster_info->locations);
        }
    }

    // Drops Section
    if (ImGui::CollapsingHeader((std::string("Drops##") + monster_id_).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderDropsSection(monster_info->drops);
    }

    ImGui::End();
}

void MonsterWindow::renderSettings()
{
    // No settings for now
}

void MonsterWindow::renderStatsSection(const monster::MonsterStats& stats)
{
    // First block: 4 columns
    if (ImGui::BeginTable((std::string("StatsTableA##") + monster_id_).c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("EXP");
        ImGui::TableSetupColumn("Gold");
        ImGui::TableSetupColumn("Training");
        ImGui::TableSetupColumn("Weak Lv");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.exp.has_value() ? std::to_string(stats.exp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.gold.has_value() ? std::to_string(stats.gold.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.training.has_value() ? std::to_string(stats.training.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.weak_level.has_value() ? std::to_string(stats.weak_level.value()).c_str() : "-");

        ImGui::EndTable();
    }

    // Second block: 5 columns
    if (ImGui::BeginTable((std::string("StatsTableB##") + monster_id_).c_str(), 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("HP");
        ImGui::TableSetupColumn("MP");
        ImGui::TableSetupColumn("Attack");
        ImGui::TableSetupColumn("Defense");
        ImGui::TableSetupColumn("Crystal Lv");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.hp.has_value() ? std::to_string(stats.hp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.mp.has_value() ? std::to_string(stats.mp.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.attack.has_value() ? std::to_string(stats.attack.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.defense.has_value() ? std::to_string(stats.defense.value()).c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::Text("%s", stats.crystal_level.has_value() ? stats.crystal_level.value().c_str() : "-");

        ImGui::EndTable();
    }
}

void MonsterWindow::renderResistancesSection(const monster::MonsterResistances& resistances)
{
    if (ImGui::BeginTable((std::string("ResistancesTable##") + monster_id_).c_str(), 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Fire");
        ImGui::TableSetupColumn("Ice");
        ImGui::TableSetupColumn("Wind");
        ImGui::TableSetupColumn("Thunder");
        ImGui::TableSetupColumn("Earth");
        ImGui::TableSetupColumn("Dark");
        ImGui::TableSetupColumn("Light");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        
        auto renderResistance = [](const std::optional<double>& value) {
            if (value.has_value())
            {
                double v = value.value();
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (v < 1.0)
                    color = ImVec4(0.8f, 0.3f, 0.3f, 1.0f); // Weak (red)
                else if (v > 1.0)
                    color = ImVec4(0.3f, 0.8f, 0.3f, 1.0f); // Resistant (green)
                
                ImGui::TextColored(color, "%.1f", v);
            }
            else
            {
                ImGui::Text("-");
            }
        };

        ImGui::TableNextColumn();
        renderResistance(resistances.fire);
        ImGui::TableNextColumn();
        renderResistance(resistances.ice);
        ImGui::TableNextColumn();
        renderResistance(resistances.wind);
        ImGui::TableNextColumn();
        renderResistance(resistances.thunder);
        ImGui::TableNextColumn();
        renderResistance(resistances.earth);
        ImGui::TableNextColumn();
        renderResistance(resistances.dark);
        ImGui::TableNextColumn();
        renderResistance(resistances.light);

        ImGui::EndTable();
    }
}

void MonsterWindow::renderLocationsSection(const std::vector<monster::MonsterLocation>& locations)
{
    for (size_t i = 0; i < locations.size(); ++i)
    {
        const auto& loc = locations[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Bullet();
        if (loc.notes.has_value() && !loc.notes.value().empty())
        {
            ImGui::Text("%s (%s)", loc.area.c_str(), loc.notes.value().c_str());
        }
        else
        {
            ImGui::Text("%s", loc.area.c_str());
        }
        ImGui::PopID();
    }
}

void MonsterWindow::renderDropsSection(const monster::MonsterDrops& drops)
{
    if (ImGui::BeginTable((std::string("DropsTable##") + monster_id_).c_str(), 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Items");

        // Normal drops
        if (!drops.normal.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.3f, 1.0f), "Normal Drop");
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.normal.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (i > 0) ImGui::SameLine();
                ImGui::Text("%s", drops.normal[i].c_str());
                if (i < drops.normal.size() - 1)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(",");
                }
                ImGui::PopID();
            }
        }

        // Rare drops
        if (!drops.rare.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.3f, 0.8f, 1.0f), "Rare Drop");
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.rare.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (i > 0) ImGui::SameLine();
                ImGui::Text("%s", drops.rare[i].c_str());
                if (i < drops.rare.size() - 1)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(",");
                }
                ImGui::PopID();
            }
        }

        // Orbs
        if (!drops.orbs.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Orbs");
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.orbs.size(); ++i)
            {
                const auto& orb = drops.orbs[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("[%s]", orb.orb_type.c_str());
                if (!orb.effect.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", orb.effect.c_str());
                }
                ImGui::PopID();
            }
        }

        // White treasure
        if (!drops.white_treasure.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "White Treasure");
            ImGui::TableNextColumn();
            for (size_t i = 0; i < drops.white_treasure.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (i > 0) ImGui::SameLine();
                ImGui::Text("%s", drops.white_treasure[i].c_str());
                if (i < drops.white_treasure.size() - 1)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(",");
                }
                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}
