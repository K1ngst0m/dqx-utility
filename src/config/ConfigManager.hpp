#pragma once

#include <string>
#include <imgui.h>

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

    // Application mode
    AppMode getAppMode() const { return app_mode_; }
    void setAppMode(AppMode m) { app_mode_ = m; }

    // GUI localization language (e.g., "en", "zh-CN")
    const char* getUILanguageCode() const { return ui_language_.c_str(); }
    void setUILanguageCode(const char* code) { ui_language_ = (code && code[0]) ? code : "en"; }
    
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

private:
    bool loadAndApply();
    bool applyDialogs(const struct DialogsSnapshot& snap); // fwd declen't exist, will implement inline

    std::string config_path_;
    std::string last_error_;
    long long last_mtime_ = 0; // epoch ms
    WindowRegistry* registry_ = nullptr;

    // global
    float ui_scale_ = 1.0f;
    bool append_logs_ = false;
    bool borderless_windows_ = false; // default to bordered (title bar visible)
    AppMode app_mode_ = AppMode::Normal;
    std::string ui_language_ = "en"; // GUI localization language code
    
    // Dialog fade settings
    bool dialog_fade_enabled_ = false;
    float dialog_fade_timeout_ = 20.0f; // seconds
    struct ImGuiStyleBackup { bool valid=false; ImGuiStyle style; };
    ImGuiStyleBackup base_;
    
    // UI requests
    bool show_global_settings_requested_ = false;
    bool quit_requested_ = false;
};

// global accessors
ConfigManager* ConfigManager_Get();
void ConfigManager_Set(ConfigManager* mgr);

// convenience save for UI
bool ConfigManager_SaveAll();
