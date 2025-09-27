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
    DialogWindow(FontManager& font_manager, ImGuiIO& io, int instance_id);
    ~DialogWindow() override;

    UIWindowType type() const override { return UIWindowType::Dialog; }
    const char* label() const override { return label_.c_str(); }

    void render(ImGuiIO& io) override;
    void renderSettings(ImGuiIO& io) override;

    DialogState& state() { return state_; }

private:
    void renderDialog(ImGuiIO& io);
    void renderSettingsPanel(ImGuiIO& io);

    FontManager& font_manager_;
    DialogState state_{};
    std::string label_;
};
