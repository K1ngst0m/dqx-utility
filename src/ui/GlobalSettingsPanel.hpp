#pragma once

#include "WindowRegistry.hpp"
#include "GlobalStateManager.hpp"
#include "DQXClarityLauncher.hpp"

#include <array>
#include <memory>
#include <string>
#include <functional>

class ConfigManager;

class GlobalSettingsPanel
{
public:
    using ExitCallback = std::function<void()>;

    GlobalSettingsPanel(WindowRegistry& registry, GlobalStateManager& global_state, ConfigManager& config, ExitCallback exitCallback = nullptr);

    void render(bool& open);

private:
    void renderStatusSection();
    void renderWindowManagementSection();
    void renderAppearanceSection();
    void renderUpdateSection();
    void renderTypeSelector();
    void renderInstanceSelector();
    void renderDQXClaritySection();
    void renderDebugSection();
    void renderProblemsPanel();

    std::string readLogFile(const std::string& path, size_t max_lines = 1000);

    WindowRegistry& registry_;
    GlobalStateManager& global_state_;
    ConfigManager& config_;
    ExitCallback exit_callback_;
    UIWindowType selected_type_ = UIWindowType::Dialog;
    int selected_index_ = 0;
    int previous_selected_index_ = -1;
    std::array<char, 128> rename_buffer_{};

    std::unique_ptr<DQXClarityLauncher> dqxc_launcher_;
    [[maybe_unused]] bool is_launching_ = false;

    // Debug log viewer state
    std::string cached_log_content_;
    float last_log_refresh_time_ = 0.0f;

    // Update check result state
    std::string update_check_hint_;
    float update_check_hint_timer_ = 0.0f;
};
