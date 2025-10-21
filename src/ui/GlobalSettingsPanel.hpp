#pragma once

#include "WindowRegistry.hpp"
#include "DQXClarityLauncher.hpp"

#include <array>
#include <memory>
#include <string>

// GlobalSettingsPanel offers application-wide settings and window management.
class GlobalSettingsPanel
{
public:
    GlobalSettingsPanel(WindowRegistry& registry);

    void render(bool& open);

private:
    void renderStatusSection();
    void renderWindowManagementSection();
    void renderAppearanceSection();
    void renderTypeSelector();
    void renderInstanceSelector();
    void renderDQXClaritySection();
    void renderDebugSection();
    void renderProblemsPanel();

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
