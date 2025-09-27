#pragma once

#include "WindowRegistry.hpp"

class FontManager;

// SettingsPanel offers selection and configuration for available UI windows.
class SettingsPanel
{
public:
    SettingsPanel(WindowRegistry& registry, FontManager& font_manager, ImGuiIO& io);

    void render();

private:
    void renderTypeSelector();
    void renderInstanceSelector(const std::vector<UIWindow*>& windows);

    WindowRegistry& registry_;
    ImGuiIO& io_;
    UIWindowType selected_type_ = UIWindowType::Dialog;
    int selected_index_ = 0;
};
