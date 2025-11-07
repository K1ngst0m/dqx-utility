#pragma once

#include "MonsterInfo.hpp"
#include <memory>
#include <optional>
#include <string>

/// MonsterManager loads monsters.jsonl and provides name-based monster lookups.
/// Supports exact and fuzzy matching for monster names.
class MonsterManager
{
public:
    MonsterManager();
    ~MonsterManager();

    /// Initialize the manager by loading monster data from the specified JSONL file.
    /// Returns true on success, false on failure (logged to plog).
    bool initialize(const std::string& monsterDataPath);

    /// Find monster data by monster ID
    /// Returns the parsed MonsterInfo if found.
    std::optional<monster::MonsterInfo> findMonsterById(const std::string& id) const;

    /// Find monster data by exact monster name (Japanese)
    /// Returns the parsed MonsterInfo if found.
    std::optional<monster::MonsterInfo> findMonsterByName(const std::string& name) const;

    /// Find monster data by name with fuzzy matching fallback
    /// Uses exact match first, then fuzzy match with 0.85 threshold.
    /// Returns the parsed MonsterInfo if found.
    std::optional<monster::MonsterInfo> findMonsterByNameFuzzy(const std::string& name) const;

    /// Get total number of loaded monsters
    std::size_t getMonsterCount() const;

    /// Annotate monster names in text with PUA markers for rendering
    /// Scans text for monster names and wraps them with Unicode PUA markers (U+E100-E102)
    /// Returns annotated text with embedded monster IDs
    std::string annotateText(const std::string& text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
