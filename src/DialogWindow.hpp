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
    
    // Test connection state (translator)
    bool testing_connection_ = false;
    std::string test_result_;
};
