#pragma once

#include "../../state/DialogStateManager.hpp"
#include "../FontManager.hpp"
#include "../WindowRegistry.hpp"
#include "../../translate/TranslateSession.hpp"
#include "DialogAnimator.hpp"
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

namespace translate { class ITranslator; }
class LabelProcessor;

class DialogWindow : public UIWindow
{
public:
    DialogWindow(FontManager& font_manager, int instance_id, const std::string& name);
    ~DialogWindow() override;

    UIWindowType type() const override { return UIWindowType::Dialog; }
    const char* displayName() const override { return name_.c_str(); }
    const char* windowLabel() const override { return window_label_.c_str(); }
    void rename(const char* new_name) override;

    void render() override;
    void renderSettings() override;


    DialogStateManager& state() { return state_; }

    // Exposed for config manager apply
    void initTranslatorIfEnabled();
    void refreshFontBinding();
    bool shouldBeRemoved() const { return should_be_removed_; }
private:
    struct PendingMsg { std::string text; std::string lang; std::string speaker; std::uint64_t seq = 0; };


    void renderDialog();
    void renderSettingsPanel();
    void renderSettingsWindow();
    void renderDialogContextMenu();

    void renderVignette(const ImVec2& win_pos, const ImVec2& win_size, float thickness, float rounding, float alpha_multiplier);
    void renderSeparator(bool hasNPC, const std::string& speaker, float content_width, float alpha_multiplier);
    void renderOutlinedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px, float wrap_width, float alpha_multiplier);

    void renderAppearanceSection();
    void renderTranslateSection();
    void renderDebugSection();
    void applyPending();

    FontManager& font_manager_;
    DialogStateManager state_{};
    std::string name_;
    std::string window_label_;
    std::string settings_window_label_;
    std::string id_suffix_;
    std::string settings_id_suffix_;
    bool show_settings_window_ = false;
    bool should_be_removed_ = false;

    // In-process messaging: pending messages and last seen seq
    PendingQueue<PendingMsg> pending_;
    std::uint64_t last_applied_seq_ = 0;
    bool appended_since_last_frame_ = false;

    std::unique_ptr<translate::ITranslator> translator_;
    std::uint64_t last_job_id_ = 0;
    std::unique_ptr<LabelProcessor> label_processor_;
    
    TranslateSession session_;

    // Test connection state (translator)
    bool testing_connection_ = false;
    std::string test_result_;
    std::string test_timestamp_;
    
    // Apply success hint (auto-clears after 5 seconds)
    std::string apply_hint_;
    float apply_hint_timer_ = 0.0f;
    DialogSettingsView settings_view_;

    // Smooth scroll animation (content-growth driven)
    bool  scroll_animating_   = false;
    bool  scroll_initialized_ = false;
    float last_scroll_max_y_  = 0.0f;
    static constexpr float SCROLL_SPEED = 800.0f;  // pixels per second (constant speed)

    // Pending translation placeholders and animation
    std::unordered_map<std::uint64_t, int> pending_segment_by_job_;
    std::unordered_set<int> failed_segments_;
    std::unordered_map<int, std::string> failed_original_text_;
    std::unordered_map<int, std::string> failed_error_messages_;
    DialogAnimator wait_anim_;

};
