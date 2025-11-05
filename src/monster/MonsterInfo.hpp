#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace monster
{

/// Represents a monster's combat statistics
struct MonsterStats
{
    std::optional<int> exp;
    std::optional<int> gold;
    std::optional<int> training;
    std::optional<int> weak_level;
    std::optional<int> hp;
    std::optional<int> mp;
    std::optional<int> attack;
    std::optional<int> defense;
    std::optional<std::string> crystal_level;
};

/// Represents elemental resistances (multiplier values)
struct MonsterResistances
{
    std::optional<double> fire;
    std::optional<double> ice;
    std::optional<double> wind;
    std::optional<double> thunder;
    std::optional<double> earth;
    std::optional<double> dark;
    std::optional<double> light;
};

/// Represents a location where a monster spawns
struct MonsterLocation
{
    std::string area;
    std::string url;
    std::optional<std::string> notes;
};

/// Represents orb drop information (宝珠)
struct MonsterOrb
{
    std::string orb_type;  // e.g., "炎宝珠"
    std::string effect;    // e.g., "メラ系呪文の極意"
};

/// Represents monster drop information
struct MonsterDrops
{
    std::vector<std::string> normal;
    std::vector<std::string> rare;
    std::vector<MonsterOrb> orbs;
    std::vector<std::string> white_treasure;
};

/// Complete monster information parsed from JSONL
struct MonsterInfo
{
    std::string id;
    std::string name;
    std::string category;
    MonsterStats stats;
    MonsterResistances resistances;
    std::vector<MonsterLocation> locations;
    MonsterDrops drops;
    std::string source_url;

    /// Original JSONL line for logging purposes
    std::string raw_json;
};

} // namespace monster
