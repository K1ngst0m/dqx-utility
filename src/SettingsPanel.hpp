#pragma once

#include "WindowRegistry.hpp"
#include "DQXClarityLauncher.hpp"

#include <array>
#include <memory>
#include <string>

// SettingsPanel offers selection and configuration for available UI windows.
class SettingsPanel
{
public:
    SettingsPanel(WindowRegistry& registry);

    void render(bool& open);

private:
    void renderTypeSelector();
    void renderInstanceSelector(const std::vector<UIWindow*>& windows);
    void renderDQXClaritySection();
    void renderDebugSection();
    
    std::string readLogFile(const std::string& path, size_t max_lines = 1000);

    WindowRegistry& registry_;
    UIWindowType selected_type_ = UIWindowType::Dialog;
    int selected_index_ = 0;
    int previous_selected_index_ = -1;
    std::array<char, 128> rename_buffer_{};

    // DQXClarity launcher
    std::unique_ptr<DQXClarityLauncher> dqxc_launcher_;
    bool is_launching_ = false;
    
    // Debug log viewer state
    std::string cached_log_content_;
    float last_log_refresh_time_ = 0.0f;
};
