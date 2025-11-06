#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "QuestHelperStateManager.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../WindowAnimator.hpp"
#include "../WindowRegistry.hpp"

class FontManager;
class ConfigManager;
class QuestManager;
class QuestHelperSettingsView;

namespace translate
{
class ITranslator;
}

class QuestHelperWindow : public UIWindow
{
    friend class QuestWindow;
public:
    QuestHelperWindow(FontManager& font_manager, ConfigManager& config, QuestManager& quest_manager, const std::string& name);
    ~QuestHelperWindow() override;

    UIWindowType type() const override { return UIWindowType::QuestHelper; }
    const char* displayName() const override { return name_.c_str(); }
    const char* windowLabel() const override { return window_label_.c_str(); }
    void rename(const char* new_name) override;

    void render() override;
    void renderSettings() override;

    bool shouldBeRemoved() const { return should_be_removed_; }
    bool isDefaultInstance() const { return is_default_instance_; }
    void setDefaultInstance(bool value) { is_default_instance_ = value; }
    void openSettings() { show_settings_window_ = true; }
    
    QuestHelperStateManager& state() { return state_; }

    void initTranslatorIfEnabled();
    void refreshFontBinding();

private:
    struct QuestStep
    {
        std::string index;
        std::string content;
        std::vector<std::string> komento;
    };

    struct StepStatus
    {
        bool has_translation = false;
        bool failed = false;
        std::string text;
        std::string error;
        std::uint64_t job_id = 0;
        std::vector<std::string> komento_translations;
        std::vector<std::uint64_t> komento_job_ids;
    };

    struct JobInfo
    {
        std::size_t step_index;
        std::size_t komento_index; // SIZE_MAX means it's a step translation, not komento
    };

    struct ActivityMonitor
    {
        void beginFrame()
        {
            active_ = false;
            hover_ = false;
        }

        void markActive() { active_ = true; }

        void setHover(bool hovered) { hover_ = hovered; }

        bool isActive() const { return active_; }

        bool hoverActive() const { return hover_; }

    private:
        bool active_ = false;
        bool hover_ = false;
    };

    void parseQuestJson(const std::string& jsonl, const std::string& game_quest_name);
    bool usingGlobalTranslation() const;
    void resetTranslatorState();
    void submitTranslationRequest();
    void submitStepTranslation(std::size_t step_index, const std::string& text, const TranslationConfig& config);
    void applyCachedTranslation(std::size_t step_index, const std::string& text);
    void handleTranslationFailure(std::size_t step_index, const std::string& message);

    // Drawer mode control
    void setDrawerMode(bool is_drawer) { is_drawer_mode_ = is_drawer; }
    bool isDrawerMode() const { return is_drawer_mode_; }
    
    void updateQuestData();
    void processTranslatorEvents();
    void renderQuestContent(float wrap_width, float font_scale);
    const TranslationConfig& activeTranslationConfig() const;
    void checkAndUpdateWindowHeight(float current_window_width);
    float calculateContentHeight(float wrap_width, float font_scale) const;
    void renderContextMenu();
    void renderSettingsWindow();

    FontManager& font_manager_;
    ConfigManager& config_;
    QuestManager& quest_manager_;
    QuestHelperStateManager state_{};
    std::unique_ptr<QuestHelperSettingsView> settings_view_;

    std::string name_;
    std::string window_label_;
    std::string settings_window_label_;
    std::string id_suffix_;
    std::string settings_id_suffix_;
    bool show_settings_window_ = false;

    TranslateSession session_;
    std::unique_ptr<translate::ITranslator> translator_;
    translate::Backend cached_backend_ = translate::Backend::OpenAI;
    translate::BackendConfig cached_config_{};
    bool translator_initialized_ = false;
    bool translator_error_reported_ = false;
    bool testing_connection_ = false;
    std::string test_result_;
    std::string test_timestamp_;
    std::string apply_hint_;
    float apply_hint_timer_ = 0.0f;

    std::string current_quest_name_;
    std::string quest_id_;
    std::string quest_name_;
    std::vector<QuestStep> steps_;
    std::vector<StepStatus> step_status_;
    std::unordered_map<std::uint64_t, JobInfo> job_lookup_;
    std::uint64_t last_seq_ = 0;

    bool should_be_removed_ = false;
    bool is_default_instance_ = false;
    ActivityMonitor activity_monitor_;
    std::uint64_t observed_global_translation_version_ = 0;
    bool last_used_global_translation_ = false;
    ui::WindowAnimator animator_;
    
    std::size_t last_content_hash_ = 0;
    float last_font_size_ = 0.0f;
    std::size_t visible_step_count_ = 3;
    
    bool is_drawer_mode_ = false;
};
