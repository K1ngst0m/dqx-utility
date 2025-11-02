#pragma once

#include <toml++/toml.h>
#include <string>

class BaseWindowState;

// Centralized TOML serialization for all window state types
class StateSerializer
{
public:
    // Serialize complete window state to TOML
    static toml::table serialize(const std::string& name, const BaseWindowState& state);
    
    // Deserialize TOML into window state
    static bool deserialize(const toml::table& tbl, BaseWindowState& state, std::string& name);

private:
    // Helper to serialize common fields (UI + translation config)
    static void serializeCommonState(toml::table& tbl, const BaseWindowState& state);
    
    // Helper to deserialize common fields
    static void deserializeCommonState(const toml::table& tbl, BaseWindowState& state);
};



