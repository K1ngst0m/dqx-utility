#pragma once

#include "state/DialogStateManager.hpp"
#include "FontManager.hpp"
#include "WindowRegistry.hpp"

#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include <cstdint>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "utils/LRUCache.hpp"

struct ImGuiIO;

namespace ipc { class TextSourceClient; }
namespace translate { class ITranslator; }
class LabelProcessor;

class DialogWindow : public UIWindow
{
public:
    DialogWindow(FontManager& font_manager, ImGuiIO& io, int instance_id, const std::string& name);
    ~DialogWindow() override;

    UIWindowType type() const override { return UIWindowType::Dialog; }
    const char* displayName() const override { return name_.c_str(); }
    const char* windowLabel() const override { return window_label_.c_str(); }
    void rename(const char* new_name) override;

    void render(ImGuiIO& io) override;
    void renderSettings(ImGuiIO& io) override;

    DialogStateManager& state() { return state_; }

    // Exposed for config manager apply
    void initTranslatorIfEnabled();
    void autoConnectIPC();
    void refreshFontBinding();
    bool shouldBeRemoved() const { return should_be_removed_; }
private:
    struct PendingMsg { std::string text; std::string lang; std::uint64_t seq = 0; };

    struct CacheDomain {
        int backend = 0; // TranslationConfig::TranslationBackend
        int target = 0;  // TranslationConfig::TargetLang
        bool operator==(const CacheDomain& o) const noexcept { return backend == o.backend && target == o.target; }
    };
    struct CacheDomainHash { std::size_t operator()(const CacheDomain& d) const noexcept { return (static_cast<std::size_t>(d.backend) * 1315423911u) ^ static_cast<std::size_t>(d.target); } };

    void renderDialog(ImGuiIO& io);
    void renderSettingsPanel(ImGuiIO& io);
    void renderSettingsWindow(ImGuiIO& io);
    void renderDialogContextMenu();

    void renderAppearanceSection();
    void renderTranslateSection();
    void renderStatusSection();
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
    std::mutex pending_mutex_;
    std::vector<PendingMsg> pending_;
    std::uint64_t last_applied_seq_ = 0;
    bool appended_since_last_frame_ = false;

    std::unique_ptr<translate::ITranslator> translator_;
    std::uint64_t last_job_id_ = 0;
    std::unique_ptr<LabelProcessor> label_processor_;
    
    // Translation cache (per dialog, segmented by backend+target)
    std::unordered_map<CacheDomain, LRUCache<std::string, std::string>, CacheDomainHash> caches_;
    std::unordered_map<CacheDomain, std::unordered_set<std::string>, CacheDomainHash> inflight_;
    std::unordered_map<std::uint64_t, std::pair<CacheDomain, std::string>> jobs_; // job_id -> (domain, key)
    std::size_t cache_capacity_ = 5000; // entries per domain
    bool cache_disabled_ = false;
    std::uint64_t cache_hits_ = 0;
    std::uint64_t cache_misses_ = 0;

    // Test connection state (translator)
    bool testing_connection_ = false;
    std::string test_result_;

    // Smooth scroll animation (content-growth driven)
    bool  scroll_animating_   = false;
    bool  scroll_initialized_ = false;
    float last_scroll_max_y_  = 0.0f;
    static constexpr float SCROLL_SPEED = 800.0f;  // pixels per second (constant speed)

    // Pending translation placeholders and animation
    std::unordered_map<std::uint64_t, int> pending_segment_by_job_;
    float waiting_anim_accum_ = 0.0f;
    int waiting_anim_phase_ = 0; // 0:".", 1:"..", 2:"...", 3:".."

    void clearCaches();
};
