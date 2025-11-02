#pragma once

#include <toml++/toml.h>
#include <string>

struct BaseWindowState;
class GlobalStateManager;

// Centralized TOML serialization for all window state types
class StateSerializer
{
public:
    // Serialize complete window state to TOML
    static toml::table serialize(const std::string& name, const BaseWindowState& state);
    
    // Deserialize TOML into window state
    static bool deserialize(const toml::table& tbl, BaseWindowState& state, std::string& name);

    // Serialize global state to TOML (includes [global], [global.translation], [app.debug])
    static toml::table serializeGlobal(const GlobalStateManager& state);
    
    // Deserialize TOML into global state
    static void deserializeGlobal(const toml::table& root, GlobalStateManager& state);

private:
    // Helper to serialize common fields (UI + translation config)
    static void serializeCommonState(toml::table& tbl, const BaseWindowState& state);
    
    // Helper to deserialize common fields
    static void deserializeCommonState(const toml::table& tbl, BaseWindowState& state);
};



