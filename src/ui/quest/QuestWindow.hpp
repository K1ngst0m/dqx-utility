#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include "../WindowRegistry.hpp"
#include "../../state/QuestStateManager.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../WindowAnimator.hpp"

namespace translate
{
class ITranslator;
}
class FontManager;
class QuestSettingsView;

class QuestWindow : public UIWindow
{
public:
    QuestWindow(FontManager& font_manager, const std::string& name, bool is_default = false);
    ~QuestWindow() override;

    UIWindowType type() const override { return UIWindowType::Quest; }

    const char* displayName() const override { return name_.c_str(); }

    const char* windowLabel() const override { return window_label_.c_str(); }

    void rename(const char* new_name) override;

    bool isDefaultInstance() const { return is_default_instance_; }

    void setDefaultInstance(bool value) { is_default_instance_ = value; }

    void render() override;
    void renderSettings() override;

    bool shouldBeRemoved() const { return should_be_removed_; }

    QuestStateManager& state() { return state_; }

    void initTranslatorIfEnabled();
    void refreshFontBinding();

private:
    enum class QuestField
    {
        SubQuest = 0,
        Title = 1,
        Description = 2,
        Rewards = 3,
        RepeatRewards = 4
    };

    struct FieldStatus
    {
        bool has_translation = false;
        bool failed = false;
        std::string text;
        std::string error;
        std::uint64_t job_id = 0;
    };

    void applyQuestUpdate();
    void renderQuestContent(float wrap_width);
    void renderRewardsRow(float wrap_width);
    void renderTranslationControls(float wrap_width);
    float estimateGridHeight(float wrap_width) const;
    void renderContextMenu();
    void renderSettingsWindow();
    void processTranslatorEvents();
    void resetTranslationState();
    void submitTranslationRequest();
    void submitFieldTranslation(QuestField field, const std::string& text, const TranslationConfig& config);
    void applyCachedTranslation(QuestField field, const std::string& text);
    void handleTranslationFailure(QuestField field, const std::string& message);
    void refreshTranslationFlags();
    std::string displayStringFor(QuestField field) const;
    FieldStatus& fieldStatus(QuestField field);
    const FieldStatus& fieldStatus(QuestField field) const;
    static std::size_t fieldIndex(QuestField field);
    std::string buildCopyBuffer() const;
    const TranslationConfig& activeTranslationConfig() const;
    bool usingGlobalTranslation() const;
    void resetTranslatorState();

    FontManager& font_manager_;
    QuestStateManager state_{};
    std::unique_ptr<QuestSettingsView> settings_view_;

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
    std::array<FieldStatus, 5> field_status_{};
    std::unordered_map<std::uint64_t, QuestField> job_lookup_;
    bool testing_connection_ = false;
    std::string test_result_;
    std::string test_timestamp_;
    std::string apply_hint_;
    float apply_hint_timer_ = 0.0f;
    std::uint64_t last_applied_seq_ = 0;
    bool should_be_removed_ = false;
    bool appended_since_last_frame_ = false;
    std::uint64_t observed_global_translation_version_ = 0;
    bool last_used_global_translation_ = false;
    ui::WindowAnimator animator_;
    bool is_default_instance_ = false;
};
