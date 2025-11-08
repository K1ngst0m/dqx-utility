#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../WindowRegistry.hpp"
#include "MonsterStateManager.hpp"
#include "MonsterSettingsView.hpp"
#include "../../translate/TranslateSession.hpp"

class FontManager;
class GlobalStateManager;
class ConfigManager;
class MonsterManager;
class MonsterSettingsView;

namespace translate
{
class ITranslator;
}

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
    MonsterStateManager& state() { return state_; }
    void refreshFontBinding();

private:
    void renderStatsSection(const monster::MonsterStats& stats);
    void renderResistancesSection(const monster::MonsterResistances& resistances);
    void renderLocationsSection(const std::vector<monster::MonsterLocation>& locations);
    void renderDropsSection(const monster::MonsterDrops& drops);
    void renderContextMenu();
    void renderSettingsWindow();
    void applyPendingResizeFlags();
    
    // Translation helpers
    void initTranslatorIfEnabled();
    void pollTranslations();
    std::string getTranslatedText(const std::string& original_text);
    void renderTextWithTooltip(const char* text, const char* tooltip);

    FontManager& font_manager_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
    MonsterManager& monster_manager_;
    
    std::string monster_id_;
    std::string name_;
    std::string window_label_;
    std::string settings_window_label_;
    std::string settings_id_suffix_;
    bool want_focus_ = true;
    bool should_be_removed_ = false;
    bool show_settings_window_ = false;
    
    MonsterStateManager state_;
    std::unique_ptr<MonsterSettingsView> settings_view_;
    
    // Translation support
    TranslateSession session_;
    std::unique_ptr<translate::ITranslator> translator_;
    translate::Backend cached_backend_ = translate::Backend::OpenAI;
    translate::BackendConfig cached_config_{};
    bool translator_initialized_ = false;
    bool translator_error_reported_ = false;
    std::unordered_map<std::string, std::string> translation_cache_;
    bool testing_connection_ = false;
    std::string test_result_;
    std::string test_timestamp_;
    std::string apply_hint_;
    float apply_hint_timer_ = 0.0f;
};
