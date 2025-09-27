#pragma once

#include "WindowRegistry.hpp"

#include <array>

// SettingsPanel offers selection and configuration for available UI windows.
class SettingsPanel
{
public:
    SettingsPanel(WindowRegistry& registry);

    void render(bool& open);

private:
    void renderTypeSelector();
    void renderInstanceSelector(const std::vector<UIWindow*>& windows);

    WindowRegistry& registry_;
    UIWindowType selected_type_ = UIWindowType::Dialog;
    int selected_index_ = 0;
    int previous_selected_index_ = -1;
    std::array<char, 128> rename_buffer_{};
};
