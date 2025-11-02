#pragma once

#include <string>
#include <cstdint>
#include <memory>

#include "../ui/GlobalStateManager.hpp"

class WindowRegistry;

// Manages application configuration including UI scale and dialog window settings
// Automatically loads config.toml at startup and saves on exit
// Supports hot-reloading of config file changes during runtime
class ConfigManager
{
public:
    using AppMode = GlobalStateManager::AppMode;

    ConfigManager(WindowRegistry& registry);
    ~ConfigManager();

    GlobalStateManager& globalState() { return global_state_; }
    const GlobalStateManager& globalState() const { return global_state_; }
    
    void pollAndApply();
    bool loadAtStartup();

    bool saveAll();

    const char* lastError() const { return last_error_.c_str(); }

    // UI requests from context menus
    void requestShowGlobalSettings() { show_global_settings_requested_ = true; }
    bool isGlobalSettingsRequested() const { return show_global_settings_requested_; }
    void consumeGlobalSettingsRequest() { show_global_settings_requested_ = false; }

    void requestQuit() { quit_requested_ = true; }
    bool isQuitRequested() const { return quit_requested_; }
    void consumeQuitRequest() { quit_requested_ = false; }

    const TranslationConfig& globalTranslationConfig() const { return global_state_.translationConfig(); }
    TranslationConfig& globalTranslationConfig() { return global_state_.translationConfig(); }

    std::uint64_t globalTranslationVersion() const { return global_state_.translationVersion(); }
    void markGlobalTranslationDirty() { global_state_.incrementTranslationVersion(); }

private:
    bool loadAndApply();

    std::string config_path_;
    std::string last_error_;
    long long last_mtime_ = 0;
    
    WindowRegistry& registry_;
    GlobalStateManager global_state_;

    // UI requests
    bool show_global_settings_requested_ = false;
    bool quit_requested_ = false;
};

// global accessors
ConfigManager* ConfigManager_Get();
void ConfigManager_Set(ConfigManager* mgr);
bool ConfigManager_SaveAll();
