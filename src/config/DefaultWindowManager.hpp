#pragma once

#include <string>
#include <memory>
#include <optional>
#include <toml++/toml.h>

#include "WindowStateOperations.hpp"

struct BaseWindowState;
class WindowRegistry;

// Manages the default window state for a specific window type
// Handles enable/disable, state persistence, and enforcement
class DefaultWindowManager
{
public:
    DefaultWindowManager(
        std::unique_ptr<IWindowStateOperations> operations,
        const char* section_name);

    // Enable or disable the default window
    void setEnabled(bool enabled, bool suppress_updates, WindowRegistry* registry);

    // Check if default window is enabled
    bool isEnabled() const { return enabled_; }

    // Enforce the current state (create/remove window as needed)
    void enforceState(WindowRegistry* registry);

    // Save state to TOML
    void saveState(toml::table& root, WindowRegistry* registry);

    // Load state from TOML
    void loadState(const toml::table& root, bool suppress_updates, WindowRegistry* registry);

    // Get the window name
    const std::string& name() const { return name_; }

    // Get the stored state (may be null)
    const BaseWindowState* state() const { return state_.get(); }

private:
    void captureCurrentState(WindowRegistry* registry);

    std::unique_ptr<IWindowStateOperations> operations_;
    std::string section_name_; // e.g., "dialogs", "quests", "quest_helpers"
    bool enabled_ = false;
    std::string name_;
    std::unique_ptr<BaseWindowState> state_;
};
