#pragma once

#include "DialogState.hpp"
#include "FontManager.hpp"
#include "WindowRegistry.hpp"

#include <string>

struct ImGuiIO;

// DialogWindow renders a single dialog instance and its settings pane.
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
    void renderDialog(ImGuiIO& io);
    void renderSettingsPanel(ImGuiIO& io);
    void renderSettingsWindow(ImGuiIO& io);
    void renderDialogOverlay();

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
};
