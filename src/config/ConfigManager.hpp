#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <map>

#include <toml++/toml.h>

struct TranslationConfig;

struct TableCallbacks
{
    std::function<void(const toml::table& section)> load;
    std::function<toml::table()> save;
};

class ConfigManager
{
public:
    ConfigManager();
    ~ConfigManager();

    bool registerTable(const std::string& path, TableCallbacks cb, std::vector<std::string> ownedKeys);
    bool load();
    bool reloadIfChanged();
    bool save();
    const toml::table& root() const;
    
    const char* lastError() const { return last_error_.c_str(); }
    
    void requestShowGlobalSettings() { show_global_settings_requested_ = true; }
    bool isGlobalSettingsRequested() const { return show_global_settings_requested_; }
    void consumeGlobalSettingsRequest() { show_global_settings_requested_ = false; }

    void requestQuit() { quit_requested_ = true; }
    bool isQuitRequested() const { return quit_requested_; }
    void consumeQuitRequest() { quit_requested_ = false; }
    

private:
    toml::table* resolveTablePath(toml::table& root, const std::string& path);
    const toml::table* resolveTablePath(const toml::table& root, const std::string& path) const;

    std::string config_path_;
    std::string last_error_;
    long long last_mtime_ = 0;
    
    struct HandlerEntry {
        std::string path;
        TableCallbacks callbacks;
        std::vector<std::string> ownedKeys;
    };
    std::vector<HandlerEntry> handlers_;
    std::unique_ptr<toml::table> root_;
    bool show_global_settings_requested_ = false;
    bool quit_requested_ = false;
};
