#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <imgui.h>

#include "ui/dialog/DialogStateManager.hpp"
#include "ui/quest/QuestStateManager.hpp"
#include "ui/quest/QuestHelperStateManager.hpp"
#include "translate/TranslationConfig.hpp"

class WindowRegistry;

// Manages application configuration including UI scale and dialog window settings
// Automatically loads config.toml at startup and saves on exit
// Supports hot-reloading of config file changes during runtime
class ConfigManager
{
public:
    enum class AppMode : int
    {
        Normal = 0,
        Borderless = 1,
        Mini = 2
    };

    ConfigManager();
    ~ConfigManager();

    // Global UI scale get/set
    float getUIScale() const { return ui_scale_; }

    void setUIScale(float scale);

    // Append logs option
    bool getAppendLogs() const { return append_logs_; }

    // Debug configuration
    int getProfilingLevel() const { return profiling_level_; }

    void setProfilingLevel(int level);

    int getLoggingLevel() const { return logging_level_; }

    void setLoggingLevel(int level);

    bool getVerbose() const { return verbose_; }

    void setVerbose(bool enabled) { verbose_ = enabled; }

    bool getCompatibilityMode() const { return compatibility_mode_; }

    void setCompatibilityMode(bool enabled) { compatibility_mode_ = enabled; }

    int getHookWaitTimeoutMs() const { return hook_wait_timeout_ms_; }

    void setHookWaitTimeoutMs(int timeout_ms) { hook_wait_timeout_ms_ = timeout_ms; }

    // Application mode
    AppMode getAppMode() const { return app_mode_; }

    void setAppMode(AppMode m) { app_mode_ = m; }

    bool getWindowAlwaysOnTop() const { return window_always_on_top_; }

    void setWindowAlwaysOnTop(bool enabled) { window_always_on_top_ = enabled; }

    // GUI localization language (e.g., "en", "zh-CN")
    const char* getUILanguageCode() const { return ui_language_.c_str(); }

    void setUILanguageCode(const char* code) { ui_language_ = (code && code[0]) ? code : "en"; }

    bool isDefaultDialogEnabled() const { return default_dialog_enabled_; }

    void setDefaultDialogEnabled(bool enabled);

    bool isDefaultQuestEnabled() const { return default_quest_enabled_; }

    void setDefaultQuestEnabled(bool enabled);

    bool isDefaultQuestHelperEnabled() const { return default_quest_helper_enabled_; }

    void setDefaultQuestHelperEnabled(bool enabled);
    void reconcileDefaultWindowStates();

    // Assign registry pointer (used for save/apply)
    void setRegistry(WindowRegistry* reg);

    WindowRegistry* registry() const { return registry_; }

    // Poll config.toml for external changes; on valid parse, apply to dialogs
    void pollAndApply();
    bool loadAtStartup();

    // Save current state of all dialogs to config.toml
    bool saveAll();

    const char* lastError() const { return last_error_.c_str(); }

    // UI requests from context menus
    void requestShowGlobalSettings() { show_global_settings_requested_ = true; }

    bool isGlobalSettingsRequested() const { return show_global_settings_requested_; }

    void consumeGlobalSettingsRequest() { show_global_settings_requested_ = false; }

    void requestQuit() { quit_requested_ = true; }

    bool isQuitRequested() const { return quit_requested_; }

    void consumeQuitRequest() { quit_requested_ = false; }

    const TranslationConfig& globalTranslationConfig() const { return global_translation_config_; }

    TranslationConfig& globalTranslationConfig() { return global_translation_config_; }

    std::uint64_t globalTranslationVersion() const { return global_translation_version_; }

    void markGlobalTranslationDirty();

private:
    bool loadAndApply();
    bool applyDialogs(const struct DialogsSnapshot& snap); // fwd declen't exist, will implement inline
    void enforceDefaultDialogState();
    void enforceDefaultQuestState();
    void enforceDefaultQuestHelperState();
    void enforceDefaultWindowStates();

    std::string config_path_;
    std::string last_error_;
    long long last_mtime_ = 0; // epoch ms
    WindowRegistry* registry_ = nullptr;

    // global
    float ui_scale_ = 1.0f;
    bool append_logs_ = false;
    bool borderless_windows_ = false; // default to bordered (title bar visible)
    AppMode app_mode_ = AppMode::Normal;
    bool window_always_on_top_ = false;
    std::string ui_language_ = "en"; // GUI localization language code

    // Debug configuration
    int profiling_level_ = 0; // 0=disabled, 1=timer, 2=tracy+timer (capped by build-time DQX_PROFILING_LEVEL)
    int logging_level_ = 4; // plog severity: 0=none, 1=fatal, 2=error, 3=warning, 4=info, 5=debug, 6=verbose
    bool verbose_ = false; // Verbose logging for dqxclarity
    bool compatibility_mode_ = false; // Auto mode (false) vs compatibility mode (true) for dialog capture
    int hook_wait_timeout_ms_ = 200; // How long to wait for hook to upgrade memory reader captures (ms)

    bool default_dialog_enabled_ = true;
    bool default_quest_enabled_ = true;
    bool default_quest_helper_enabled_ = true;
    std::string default_dialog_name_;
    std::string default_quest_name_;
    std::string default_quest_helper_name_;
    std::optional<DialogStateManager> default_dialog_state_;
    std::optional<QuestStateManager> default_quest_state_;
    std::optional<QuestHelperStateManager> default_quest_helper_state_;
    bool suppress_default_window_updates_ = false;

    struct ImGuiStyleBackup
    {
        bool valid = false;
        ImGuiStyle style;
    };

    ImGuiStyleBackup base_;

    // UI requests
    bool show_global_settings_requested_ = false;
    bool quit_requested_ = false;

    TranslationConfig global_translation_config_;
    std::uint64_t global_translation_version_ = 1;
};

// global accessors
ConfigManager* ConfigManager_Get();
void ConfigManager_Set(ConfigManager* mgr);

// convenience save for UI
bool ConfigManager_SaveAll();
