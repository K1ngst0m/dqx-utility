#pragma once

#include "../translate/TranslationConfig.hpp"

#include <imgui.h>
#include <string>
#include <cstdint>

// Manages all application-wide global state
class GlobalStateManager
{
public:
    enum class AppMode : int
    {
        Normal = 0,
        Borderless = 1,
        Mini = 2
    };

    GlobalStateManager();

    void applyDefaults();

    // UI Scale
    float uiScale() const { return ui_scale_; }
    void setUIScale(float scale);
    void applyUIScale(float scale);

    // General settings
    bool appendLogs() const { return append_logs_; }
    void setAppendLogs(bool enabled) { append_logs_ = enabled; }

    bool borderlessWindows() const { return borderless_windows_; }
    void setBorderlessWindows(bool enabled) { borderless_windows_ = enabled; }

    AppMode appMode() const { return app_mode_; }
    void setAppMode(AppMode mode) { app_mode_ = mode; }

    bool windowAlwaysOnTop() const { return window_always_on_top_; }
    void setWindowAlwaysOnTop(bool enabled) { window_always_on_top_ = enabled; }

    const std::string& uiLanguage() const { return ui_language_; }
    void setUILanguage(const std::string& lang) { ui_language_ = lang; }

    // Debug settings
    int profilingLevel() const { return profiling_level_; }
    void setProfilingLevel(int level) { profiling_level_ = level; }

    int loggingLevel() const { return logging_level_; }
    void setLoggingLevel(int level) { logging_level_ = level; }

    bool verbose() const { return verbose_; }
    void setVerbose(bool enabled) { verbose_ = enabled; }

    bool compatibilityMode() const { return compatibility_mode_; }
    void setCompatibilityMode(bool enabled) { compatibility_mode_ = enabled; }

    int hookWaitTimeoutMs() const { return hook_wait_timeout_ms_; }
    void setHookWaitTimeoutMs(int timeout_ms) { hook_wait_timeout_ms_ = timeout_ms; }

    // Default window flags
    bool defaultDialogEnabled() const { return default_dialog_enabled_; }
    void setDefaultDialogEnabled(bool enabled) { default_dialog_enabled_ = enabled; }

    bool defaultQuestEnabled() const { return default_quest_enabled_; }
    void setDefaultQuestEnabled(bool enabled) { default_quest_enabled_ = enabled; }

    bool defaultQuestHelperEnabled() const { return default_quest_helper_enabled_; }
    void setDefaultQuestHelperEnabled(bool enabled) { default_quest_helper_enabled_ = enabled; }

    // Translation config
    const TranslationConfig& translationConfig() const { return translation_config_; }
    TranslationConfig& translationConfig() { return translation_config_; }

    std::uint64_t translationVersion() const { return translation_version_; }
    void incrementTranslationVersion();

    // ImGui style backup (for UI scale)
    struct ImGuiStyleBackup
    {
        bool valid = false;
        ImGuiStyle style;
    };

    ImGuiStyleBackup& styleBackup() { return style_backup_; }
    const ImGuiStyleBackup& styleBackup() const { return style_backup_; }

private:
    // UI settings
    float ui_scale_ = 1.0f;
    bool append_logs_ = false;
    bool borderless_windows_ = false;
    AppMode app_mode_ = AppMode::Normal;
    bool window_always_on_top_ = false;
    std::string ui_language_ = "en";

    // Debug settings
    int profiling_level_ = 0;
    int logging_level_ = 4;
    bool verbose_ = false;
    bool compatibility_mode_ = false;
    int hook_wait_timeout_ms_ = 200;

    // Default window flags
    bool default_dialog_enabled_ = true;
    bool default_quest_enabled_ = true;
    bool default_quest_helper_enabled_ = false;

    // Translation configuration
    TranslationConfig translation_config_;
    std::uint64_t translation_version_ = 1;

    // ImGui style backup
    ImGuiStyleBackup style_backup_;
};

