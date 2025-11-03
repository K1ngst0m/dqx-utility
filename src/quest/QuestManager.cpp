#include "QuestManager.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <plog/Log.h>
#include <unordered_map>

#include "dqxclarity/api/quest_message.hpp"
#include "processing/JapaneseFuzzyMatcher.hpp"
#include "services/DQXClarityService.hpp"
#include "services/DQXClarityLauncher.hpp"

using json = nlohmann::json;

struct QuestManager::Impl
{
    // Name-based lookup: quest_name (Japanese) -> original JSONL line
    std::unordered_map<std::string, std::string> quests_by_name_;

    // ID-based lookup: quest_id -> original JSONL line (for future use)
    std::unordered_map<std::string, std::string> quests_by_id_;

    // Track last sequence number to detect quest changes
    std::uint64_t last_seq_ = 0;

    // Flag indicating successful initialization
    bool initialized_ = false;

    // Fuzzy matcher for fallback quest name matching
    processing::JapaneseFuzzyMatcher fuzzy_matcher_;
};

QuestManager::QuestManager()
    : impl_(std::make_unique<Impl>())
{
}

QuestManager::~QuestManager() = default;

bool QuestManager::initialize(const std::string& questDataPath)
{
    std::ifstream file(questDataPath);
    if (!file.is_open())
    {
        PLOG_ERROR << "QuestManager: Failed to open quest data file: " << questDataPath;
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    std::size_t loaded_count = 0;
    std::size_t error_count = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        try
        {
            // Parse JSONL line
            json quest_obj = json::parse(line);

            // Extract id and name fields
            if (!quest_obj.contains("id") || !quest_obj.contains("name"))
            {
                PLOG_WARNING << "QuestManager: Line " << line_number << " missing 'id' or 'name' field, skipping";
                ++error_count;
                continue;
            }

            std::string id = quest_obj["id"].get<std::string>();
            std::string name = quest_obj["name"].get<std::string>();

            // Store original JSONL line for both lookups
            impl_->quests_by_id_[id] = line;
            impl_->quests_by_name_[name] = line;

            ++loaded_count;
        }
        catch (const json::parse_error& e)
        {
            PLOG_ERROR << "QuestManager: JSON parse error at line " << line_number << ": " << e.what();
            ++error_count;
        }
        catch (const std::exception& e)
        {
            PLOG_ERROR << "QuestManager: Error processing line " << line_number << ": " << e.what();
            ++error_count;
        }
    }

    file.close();

    if (loaded_count == 0)
    {
        PLOG_ERROR << "QuestManager: No quests loaded from " << questDataPath;
        return false;
    }

    PLOG_INFO << "QuestManager: Loaded " << loaded_count << " quests from " << questDataPath;
    if (error_count > 0)
    {
        PLOG_WARNING << "QuestManager: Encountered " << error_count << " errors during loading";
    }

    impl_->initialized_ = true;
    return true;
}

void QuestManager::update()
{
    // Don't poll if not initialized
    if (!impl_->initialized_)
        return;

    // Get DQXClarityService instance
    auto* launcher = DQXClarityService_Get();
    if (!launcher)
        return;

    // Poll for latest quest message
    dqxclarity::QuestMessage msg;
    if (!launcher->getLatestQuest(msg))
        return;

    // Only process if sequence number has changed
    if (msg.seq == 0 || msg.seq == impl_->last_seq_)
        return;

    impl_->last_seq_ = msg.seq;

    // Perform name-based lookup (exact + fuzzy fallback)
    auto quest_data = findQuestByName(msg.quest_name);

    if (quest_data.has_value())
    {
        // SUCCESS: Log the complete original JSONL entry with structured prefix
        PLOG_INFO << "QUEST: " << quest_data.value();
    }
    else
    {
        // FAILURE: Log warning for data maintenance feedback
        // Do NOT output QUEST log if no match found
        if (!msg.quest_name.empty())
        {
            PLOG_WARNING << "QuestManager: Quest name lookup failed (exact + fuzzy). "
                         << "Name: '" << msg.quest_name << "'";
        }
    }
}

std::optional<std::string> QuestManager::findQuestById(const std::string& id) const
{
    auto it = impl_->quests_by_id_.find(id);
    if (it != impl_->quests_by_id_.end())
        return it->second;
    return std::nullopt;
}

std::optional<std::string> QuestManager::findQuestByName(const std::string& name) const
{
    // Fast path: Try exact match first (O(1) hash lookup)
    auto it = impl_->quests_by_name_.find(name);
    if (it != impl_->quests_by_name_.end())
        return it->second;

    // Fallback: Fuzzy matching with 0.85 threshold
    // Build candidates list from all quest names
    std::vector<std::string> candidates;
    candidates.reserve(impl_->quests_by_name_.size());
    for (const auto& [quest_name, _] : impl_->quests_by_name_)
    {
        candidates.push_back(quest_name);
    }

    // Find best fuzzy match
    constexpr double threshold = 0.85;
    auto match = impl_->fuzzy_matcher_.findBestMatch(name, candidates, threshold, processing::MatchAlgorithm::Ratio);

    if (match.has_value())
    {
        // Found a fuzzy match - retrieve the JSONL data
        auto match_it = impl_->quests_by_name_.find(match->matched);
        if (match_it != impl_->quests_by_name_.end())
        {
            PLOG_INFO << "QuestManager: Fuzzy matched '" << name << "' to '" << match->matched 
                      << "' (score: " << match->score << ")";
            return match_it->second;
        }
    }

    // No match found (exact or fuzzy)
    return std::nullopt;
}
