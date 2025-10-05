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
    ConfigManager();
    ~ConfigManager();

    // Global UI scale get/set
    float getUIScale() const { return ui_scale_; }
    void setUIScale(float scale);
    
    // Append logs option
    bool getAppendLogs() const { return append_logs_; }

    // Borderless windows (hide title bar on dialog windows)
    bool getBorderlessWindows() const { return borderless_windows_; }
    void setBorderlessWindows(bool v) { borderless_windows_ = v; }

    // Assign registry pointer (used for save/apply)
    void setRegistry(WindowRegistry* reg);

    // Poll config.toml for external changes; on valid parse, apply to dialogs
    void pollAndApply();
    bool loadAtStartup();

    // Save current state of all dialogs to config.toml
    bool saveAll();

    const char* lastError() const { return last_error_.c_str(); }

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
    struct ImGuiStyleBackup { bool valid=false; ImGuiStyle style; };
    ImGuiStyleBackup base_;
};

// global accessors
ConfigManager* ConfigManager_Get();
void ConfigManager_Set(ConfigManager* mgr);

// convenience save for UI
bool ConfigManager_SaveAll();
