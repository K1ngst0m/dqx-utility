#pragma once

#include "DialogState.hpp"
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

    DialogState& state() { return state_; }
private:
    struct PendingMsg { std::string text; std::string lang; std::uint64_t seq = 0; };

    void renderDialog(ImGuiIO& io);
    void renderSettingsPanel(ImGuiIO& io);
    void renderSettingsWindow(ImGuiIO& io);
    void renderDialogOverlay();
    void applyPending();

    FontManager& font_manager_;
    DialogState state_{};
    std::string name_;
    std::string window_label_;
    std::string settings_window_label_;
    std::string id_suffix_;
    std::string settings_id_suffix_;
    std::string overlay_id_suffix_;
    bool show_settings_window_ = false;
    float overlay_visibility_ = 0.0f;

    std::unique_ptr<ipc::TextSourceClient> client_;
    std::mutex pending_mutex_;
    std::vector<PendingMsg> pending_;
    std::uint64_t last_applied_seq_ = 0;
    bool appended_since_last_frame_ = false;
    std::array<char, 512> last_error_{};
};
