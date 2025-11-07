#pragma once

#include "DialogStateManager.hpp"
#include "../FontManager.hpp"
#include "../WindowRegistry.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../WindowAnimator.hpp"
#include "../../utils/PendingQueue.hpp"
#include "DialogSettingsView.hpp"

#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include <cstdint>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <functional>

class ConfigManager;
class GlobalStateManager;

namespace translate
{
class ITranslator;
struct BackendConfig;
enum class Backend;
} // namespace translate

namespace processing
{
class TextPipeline;
class ITextNormalizer;
}

class DialogWindow : public UIWindow
{
public:
    DialogWindow(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config, int instance_id, const std::string& name, bool is_default = false);
    ~DialogWindow() override;

    UIWindowType type() const override { return UIWindowType::Dialog; }

    const char* displayName() const override { return name_.c_str(); }

    const char* windowLabel() const override { return window_label_.c_str(); }

    void rename(const char* new_name) override;

    bool isDefaultInstance() const { return is_default_instance_; }

    void setDefaultInstance(bool value) { is_default_instance_ = value; }

    void render() override;
    void renderSettings() override;

    DialogStateManager& state() { return state_; }

    // Exposed for config manager apply
    void initTranslatorIfEnabled();
    void refreshFontBinding();

    bool shouldBeRemoved() const { return should_be_removed_; }

    void reinitializePlaceholder();

private:
    struct PendingMsg
    {
        bool is_corner_text = false;
        std::string text;
        std::string speaker;
        std::uint64_t seq = 0;
    };

    void renderDialog();
    void renderSettingsPanel();
    void renderSettingsWindow();
    void renderDialogContextMenu();
    void ensurePlaceholderEntry();
    enum class PlaceholderState
    {
        Waiting,
        Ready,
        Error
    };
    void setPlaceholderText(const std::string& text, PlaceholderState state);
    void refreshPlaceholderStatus();
    int appendSegmentInternal(const std::string& speaker, const std::string& text);
    void resetPlaceholder();

    void renderVignette(const ImVec2& win_pos, const ImVec2& win_size, float thickness, float rounding,
                        float alpha_multiplier);
    void renderSeparator(bool hasNPC, const std::string& speaker, float content_width);
    void renderOutlinedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px,
                            float wrap_width);

    void renderAppearanceSection();
    void renderTranslateSection();
    void applyPending();
    const TranslationConfig& activeTranslationConfig() const;
    bool usingGlobalTranslation() const;
    void resetTranslatorState();

    FontManager& font_manager_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
    DialogStateManager state_{};
    std::string name_;
    std::string window_label_;
    std::string settings_window_label_;
    std::string id_suffix_;
    std::string settings_id_suffix_;
    bool show_settings_window_ = false;
    bool should_be_removed_ = false;
    bool is_default_instance_ = false;

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

    // In-process messaging: pending messages and last seen seq
    PendingQueue<PendingMsg> pending_;
    std::uint64_t last_applied_seq_ = 0;
    std::uint64_t last_corner_text_seq_ = 0;
    ActivityMonitor activity_monitor_;
    bool scroll_to_bottom_requested_ = false;

    std::unique_ptr<translate::ITranslator> translator_;
    std::uint64_t last_job_id_ = 0;
    std::unique_ptr<processing::TextPipeline> text_pipeline_;
    std::unique_ptr<processing::ITextNormalizer> text_normalizer_;

    TranslateSession session_;
    translate::BackendConfig cached_translator_config_{};
    translate::Backend cached_backend_ = translate::Backend::OpenAI;
    bool translator_initialized_ = false;
    bool translator_error_reported_ = false;
    bool placeholder_active_ = false;
    PlaceholderState placeholder_state_ = PlaceholderState::Waiting;
    std::string placeholder_base_text_;

    // Test connection state (translator)
    bool testing_connection_ = false;
    std::string test_result_;
    std::string test_timestamp_;

    // Apply success hint (auto-clears after 5 seconds)
    std::string apply_hint_;
    float apply_hint_timer_ = 0.0f;
    DialogSettingsView settings_view_;

    // Pending translation placeholders and animation
    std::unordered_map<std::uint64_t, int> pending_segment_by_job_;
    std::unordered_set<int> failed_segments_;
    std::unordered_map<int, std::string> failed_original_text_;
    std::unordered_map<int, std::string> failed_error_messages_;
    ui::WindowAnimator animator_;

    std::uint64_t observed_global_translation_version_ = 0;
    bool last_used_global_translation_ = false;
};
