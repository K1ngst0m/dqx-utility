#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <imgui.h>

#include "../state/DialogStateManager.hpp"
#include "../state/QuestStateManager.hpp"
#include "../state/TranslationConfig.hpp"

class WindowRegistry;

// Manages application configuration including UI scale and dialog window settings
// Automatically loads config.toml at startup and saves on exit
// Supports hot-reloading of config file changes during runtime
class ConfigManager
{
public:
    enum class AppMode : int { Normal = 0, Borderless = 1, Mini = 2 };

    ConfigManager();
    ~ConfigManager();

    // Global UI scale get/set
    float getUIScale() const { return ui_scale_; }
    void setUIScale(float scale);
    
    // Append logs option
    bool getAppendLogs() const { return append_logs_; }

    bool getVerboseLogging() const { return verbose_logging_; }
    void setVerboseLogging(bool enabled);
    void setForceVerboseLogging(bool enabled);
    bool isForceVerboseLogging() const { return force_verbose_logging_; }

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
    void reconcileDefaultWindowStates();
    
    // Dialog auto-fade settings
    bool getDialogFadeEnabled() const { return dialog_fade_enabled_; }
    void setDialogFadeEnabled(bool enabled) { dialog_fade_enabled_ = enabled; }
    float getDialogFadeTimeout() const { return dialog_fade_timeout_; }
    void setDialogFadeTimeout(float timeout) { dialog_fade_timeout_ = timeout; }

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
    void applyVerboseSetting();
    void enforceDefaultDialogState();
    void enforceDefaultQuestState();
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
    bool verbose_logging_ = false;
    bool force_verbose_logging_ = false;
    bool default_dialog_enabled_ = true;
    bool default_quest_enabled_ = true;
    std::string default_dialog_name_;
    std::string default_quest_name_;
    std::optional<DialogStateManager> default_dialog_state_;
    std::optional<QuestStateManager> default_quest_state_;
    bool suppress_default_window_updates_ = false;
    
    // Dialog fade settings
    bool dialog_fade_enabled_ = false;
    float dialog_fade_timeout_ = 20.0f; // seconds
    struct ImGuiStyleBackup { bool valid=false; ImGuiStyle style; };
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
