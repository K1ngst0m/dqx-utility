#pragma once

#include <string>
#include <cstdint>
#include <memory>

#include "../ui/GlobalStateManager.hpp"

class WindowRegistry;
class DefaultWindowManager;

// Manages application configuration including UI scale and dialog window settings
// Automatically loads config.toml at startup and saves on exit
// Supports hot-reloading of config file changes during runtime
class ConfigManager
{
public:
    using AppMode = GlobalStateManager::AppMode;

    ConfigManager();
    ~ConfigManager();

    // Global state accessor
    GlobalStateManager& globalState() { return global_state_; }
    const GlobalStateManager& globalState() const { return global_state_; }

    bool isDefaultDialogEnabled() const { return global_state_.defaultDialogEnabled(); }
    void setDefaultDialogEnabled(bool enabled);

    bool isDefaultQuestEnabled() const { return global_state_.defaultQuestEnabled(); }
    void setDefaultQuestEnabled(bool enabled);

    bool isDefaultQuestHelperEnabled() const { return global_state_.defaultQuestHelperEnabled(); }
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

    const TranslationConfig& globalTranslationConfig() const { return global_state_.translationConfig(); }
    TranslationConfig& globalTranslationConfig() { return global_state_.translationConfig(); }

    std::uint64_t globalTranslationVersion() const { return global_state_.translationVersion(); }
    void markGlobalTranslationDirty() { global_state_.incrementTranslationVersion(); }

private:
    bool loadAndApply();
    void enforceDefaultWindowStates();

    std::string config_path_;
    std::string last_error_;
    long long last_mtime_ = 0;
    WindowRegistry* registry_ = nullptr;

    // Global state
    GlobalStateManager global_state_;

    // Default window managers (generic, type-agnostic)
    std::unique_ptr<DefaultWindowManager> default_dialog_mgr_;
    std::unique_ptr<DefaultWindowManager> default_quest_mgr_;
    std::unique_ptr<DefaultWindowManager> default_quest_helper_mgr_;
    bool suppress_default_window_updates_ = false;

    // UI requests
    bool show_global_settings_requested_ = false;
    bool quit_requested_ = false;
};

// global accessors
ConfigManager* ConfigManager_Get();
void ConfigManager_Set(ConfigManager* mgr);

// convenience save for UI
bool ConfigManager_SaveAll();
