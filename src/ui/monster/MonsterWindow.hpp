#pragma once

#include <string>
#include <vector>
#include "../WindowRegistry.hpp"

class FontManager;
class GlobalStateManager;
class ConfigManager;
class MonsterManager;

namespace monster
{
struct MonsterStats;
struct MonsterResistances;
struct MonsterLocation;
struct MonsterDrops;
}

class MonsterWindow : public UIWindow
{
public:
    MonsterWindow(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config,
                  MonsterManager& monster_manager, const std::string& monster_id, const std::string& name);
    ~MonsterWindow() override;

    UIWindowType type() const override { return UIWindowType::Monster; }
    const char* displayName() const override { return name_.c_str(); }
    const char* windowLabel() const override { return window_label_.c_str(); }
    void rename(const char* new_name) override;
    void render() override;
    void renderSettings() override;

    const std::string& monsterId() const { return monster_id_; }
    bool shouldBeRemoved() const { return should_be_removed_; }
    void requestFocus() { want_focus_ = true; }

private:
    void renderStatsSection(const monster::MonsterStats& stats);
    void renderResistancesSection(const monster::MonsterResistances& resistances);
    void renderLocationsSection(const std::vector<monster::MonsterLocation>& locations);
    void renderDropsSection(const monster::MonsterDrops& drops);

    FontManager& font_manager_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
    MonsterManager& monster_manager_;
    
    std::string monster_id_;
    std::string name_;
    std::string window_label_;
    bool want_focus_ = true;
    bool should_be_removed_ = false;
};
