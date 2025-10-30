#pragma once

#include <memory>
#include <optional>
#include <string>

/// QuestManager loads quests.jsonl and provides name-based quest lookups.
/// Polls DQXClarityService for quest changes and logs structured JSONL data via plog.
class QuestManager
{
public:
    QuestManager();
    ~QuestManager();

    /// Initialize the manager by loading quest data from the specified JSONL file.
    /// Returns true on success, false on failure (logged to plog).
    bool initialize(const std::string& questDataPath);

    /// Poll DQXClarityService for quest message changes.
    /// When a new quest is detected, performs name-based lookup and logs results.
    /// Call this each frame from the main loop.
    void update();

    /// Find quest data by quest ID (for future use)
    /// Returns the original JSONL line if found.
    std::optional<std::string> findQuestById(const std::string& id) const;

    /// Find quest data by exact quest name (Japanese)
    /// Returns the original JSONL line if found.
    std::optional<std::string> findQuestByName(const std::string& name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
