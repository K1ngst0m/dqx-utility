#include "MonsterManager.hpp"
#include "../processing/JapaneseFuzzyMatcher.hpp"
#include "../processing/NFKCTextNormalizer.hpp"
#include "../processing/TextUtils.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <plog/Log.h>

using json = nlohmann::json;

namespace
{

/// Helper: Parse MonsterStats from JSON
monster::MonsterStats parseStats(const json& stats_json)
{
    monster::MonsterStats stats;
    
    if (stats_json.contains("exp") && !stats_json["exp"].is_null())
        stats.exp = stats_json["exp"].get<int>();
    
    if (stats_json.contains("gold") && !stats_json["gold"].is_null())
        stats.gold = stats_json["gold"].get<int>();
    
    if (stats_json.contains("training") && !stats_json["training"].is_null())
        stats.training = stats_json["training"].get<int>();
    
    if (stats_json.contains("weak_level") && !stats_json["weak_level"].is_null())
        stats.weak_level = stats_json["weak_level"].get<int>();
    
    if (stats_json.contains("hp") && !stats_json["hp"].is_null())
        stats.hp = stats_json["hp"].get<int>();
    
    if (stats_json.contains("mp") && !stats_json["mp"].is_null())
        stats.mp = stats_json["mp"].get<int>();
    
    if (stats_json.contains("attack") && !stats_json["attack"].is_null())
        stats.attack = stats_json["attack"].get<int>();
    
    if (stats_json.contains("defense") && !stats_json["defense"].is_null())
        stats.defense = stats_json["defense"].get<int>();
    
    if (stats_json.contains("crystal_level") && !stats_json["crystal_level"].is_null())
        stats.crystal_level = stats_json["crystal_level"].get<std::string>();
    
    return stats;
}

/// Helper: Parse MonsterResistances from JSON
monster::MonsterResistances parseResistances(const json& resist_json)
{
    monster::MonsterResistances resistances;
    
    if (resist_json.contains("fire") && !resist_json["fire"].is_null())
        resistances.fire = resist_json["fire"].get<double>();
    
    if (resist_json.contains("ice") && !resist_json["ice"].is_null())
        resistances.ice = resist_json["ice"].get<double>();
    
    if (resist_json.contains("wind") && !resist_json["wind"].is_null())
        resistances.wind = resist_json["wind"].get<double>();
    
    if (resist_json.contains("thunder") && !resist_json["thunder"].is_null())
        resistances.thunder = resist_json["thunder"].get<double>();
    
    if (resist_json.contains("earth") && !resist_json["earth"].is_null())
        resistances.earth = resist_json["earth"].get<double>();
    
    if (resist_json.contains("dark") && !resist_json["dark"].is_null())
        resistances.dark = resist_json["dark"].get<double>();
    
    if (resist_json.contains("light") && !resist_json["light"].is_null())
        resistances.light = resist_json["light"].get<double>();
    
    return resistances;
}

/// Helper: Parse MonsterLocations from JSON array
std::vector<monster::MonsterLocation> parseLocations(const json& locations_json)
{
    std::vector<monster::MonsterLocation> locations;
    
    for (const auto& loc : locations_json)
    {
        monster::MonsterLocation location;
        location.area = loc.value("name", loc.value("area", ""));
        location.url = loc.value("url", "");
        
        if (loc.contains("note") && !loc["note"].is_null())
            location.notes = loc["note"].get<std::string>();
        else if (loc.contains("notes") && !loc["notes"].is_null())
            location.notes = loc["notes"].get<std::string>();
        
        locations.push_back(location);
    }
    
    return locations;
}

/// Helper: Parse MonsterDrops from JSON
monster::MonsterDrops parseDrops(const json& drops_json)
{
    monster::MonsterDrops drops;
    
    // Normal drops
    if (drops_json.contains("normal") && drops_json["normal"].is_array())
    {
        for (const auto& item : drops_json["normal"])
        {
            if (item.is_string())
                drops.normal.push_back(item.get<std::string>());
            else if (item.is_object() && item.contains("name"))
                drops.normal.push_back(item["name"].get<std::string>());
        }
    }
    
    // Rare drops
    if (drops_json.contains("rare") && drops_json["rare"].is_array())
    {
        for (const auto& item : drops_json["rare"])
        {
            if (item.is_string())
                drops.rare.push_back(item.get<std::string>());
            else if (item.is_object() && item.contains("name"))
                drops.rare.push_back(item["name"].get<std::string>());
        }
    }
    
    // Orbs (宝珠)
    if (drops_json.contains("orbs") && drops_json["orbs"].is_array())
    {
        for (const auto& orb : drops_json["orbs"])
        {
            monster::MonsterOrb orb_info;
            orb_info.orb_type = orb.value("type", "");
            orb_info.effect = orb.value("effect", "");
            drops.orbs.push_back(orb_info);
        }
    }
    
    // White treasure
    if (drops_json.contains("white_treasure") && drops_json["white_treasure"].is_array())
    {
        for (const auto& item : drops_json["white_treasure"])
        {
            if (item.is_string())
                drops.white_treasure.push_back(item.get<std::string>());
            else if (item.is_object() && item.contains("name"))
                drops.white_treasure.push_back(item["name"].get<std::string>());
        }
    }
    
    return drops;
}

/// Helper: Parse complete MonsterInfo from JSONL line
std::optional<monster::MonsterInfo> parseMonsterInfo(const std::string& jsonl_line)
{
    try
    {
        json monster_json = json::parse(jsonl_line);
        
        // Validate required fields
        if (!monster_json.contains("id") || !monster_json.contains("name"))
        {
            return std::nullopt;
        }
        
        monster::MonsterInfo info;
        
        if (monster_json["id"].is_string())
        {
            info.id = monster_json["id"].get<std::string>();
        }
        else if (monster_json["id"].is_number())
        {
            info.id = std::to_string(monster_json["id"].get<int>());
        }
        else
        {
            return std::nullopt;
        }
        
        info.name = monster_json["name"].get<std::string>();
        info.category = monster_json.value("category", "");
        info.source_url = monster_json.value("source_url", "");
        info.raw_json = jsonl_line;
        
        // Parse optional structured fields
        if (monster_json.contains("stats") && monster_json["stats"].is_object())
            info.stats = parseStats(monster_json["stats"]);
        
        if (monster_json.contains("resistances") && monster_json["resistances"].is_object())
            info.resistances = parseResistances(monster_json["resistances"]);
        
        if (monster_json.contains("locations") && monster_json["locations"].is_array())
            info.locations = parseLocations(monster_json["locations"]);
        
        if (monster_json.contains("drops") && monster_json["drops"].is_object())
            info.drops = parseDrops(monster_json["drops"]);
        
        return info;
    }
    catch (const json::parse_error& e)
    {
        PLOG_ERROR << "MonsterManager: JSON parse error: " << e.what();
        return std::nullopt;
    }
    catch (const std::exception& e)
    {
        PLOG_ERROR << "MonsterManager: Error parsing monster: " << e.what();
        return std::nullopt;
    }
}

} // anonymous namespace

struct MonsterManager::Impl
{
    // Name-based lookup: monster_name (Japanese) -> MonsterInfo
    std::unordered_map<std::string, monster::MonsterInfo> monsters_by_name_;

    // Normalized (NFKC) name-based lookup
    std::unordered_map<std::string, monster::MonsterInfo> monsters_by_name_nfkc_;

    // ID-based lookup: monster_id -> MonsterInfo
    std::unordered_map<std::string, monster::MonsterInfo> monsters_by_id_;

    // Flag indicating successful initialization
    bool initialized_ = false;

    // Fuzzy matcher for fallback monster name matching
    processing::JapaneseFuzzyMatcher fuzzy_matcher_;
};

MonsterManager::MonsterManager()
    : impl_(std::make_unique<Impl>())
{
}

MonsterManager::~MonsterManager() = default;

bool MonsterManager::initialize(const std::string& monsterDataPath)
{
    std::ifstream file(monsterDataPath);
    if (!file.is_open())
    {
        PLOG_ERROR << "MonsterManager: Failed to open monster data file: " << monsterDataPath;
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    std::size_t loaded_count = 0;
    std::size_t error_count = 0;

    processing::NFKCTextNormalizer normalizer;

    while (std::getline(file, line))
    {
        ++line_number;

        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        auto monster_info = parseMonsterInfo(line);
        if (monster_info.has_value())
        {
            // Store in both maps
            impl_->monsters_by_id_[monster_info->id] = *monster_info;
            impl_->monsters_by_name_[monster_info->name] = *monster_info;
            // Normalized key
            std::string norm_name = normalizer.normalize(monster_info->name);
            impl_->monsters_by_name_nfkc_[norm_name] = *monster_info;
            ++loaded_count;
        }
        else
        {
            PLOG_WARNING << "MonsterManager: Failed to parse line " << line_number;
            ++error_count;
        }
    }

    file.close();

    if (loaded_count == 0)
    {
        PLOG_ERROR << "MonsterManager: No monsters loaded from " << monsterDataPath;
        return false;
    }

    PLOG_INFO << "MonsterManager: Loaded " << loaded_count << " monsters from " << monsterDataPath;
    if (error_count > 0)
    {
        PLOG_WARNING << "MonsterManager: Encountered " << error_count << " errors during loading";
    }

    impl_->initialized_ = true;
    return true;
}

std::optional<monster::MonsterInfo> MonsterManager::findMonsterById(const std::string& id) const
{
    auto it = impl_->monsters_by_id_.find(id);
    if (it != impl_->monsters_by_id_.end())
        return it->second;
    return std::nullopt;
}

std::optional<monster::MonsterInfo> MonsterManager::findMonsterByName(const std::string& name) const
{
    // Exact match (normalized)
    processing::NFKCTextNormalizer normalizer;
    std::string norm = normalizer.normalize(name);
    auto itn = impl_->monsters_by_name_nfkc_.find(norm);
    if (itn != impl_->monsters_by_name_nfkc_.end())
        return itn->second;
    return std::nullopt;
}

std::optional<monster::MonsterInfo> MonsterManager::findMonsterByNameFuzzy(const std::string& name) const
{
    // Fast path: Try exact match first (O(1) hash lookup)
    auto it = impl_->monsters_by_name_.find(name);
    if (it != impl_->monsters_by_name_.end())
        return it->second;

    // Fallback: Fuzzy matching with 0.85 threshold
    // Build candidates list from all monster names
    std::vector<std::string> candidates;
    candidates.reserve(impl_->monsters_by_name_.size());
    for (const auto& [monster_name, _] : impl_->monsters_by_name_)
    {
        candidates.push_back(monster_name);
    }

    // Find best fuzzy match
    constexpr double threshold = 0.85;
    auto match = impl_->fuzzy_matcher_.findBestMatch(name, candidates, threshold, processing::MatchAlgorithm::Ratio);

    if (match.has_value())
    {
        // Found a fuzzy match - retrieve the MonsterInfo
        auto match_it = impl_->monsters_by_name_.find(match->matched);
        if (match_it != impl_->monsters_by_name_.end())
        {
            PLOG_INFO << "MonsterManager: Fuzzy matched '" << name << "' to '" << match->matched 
                      << "' (score: " << match->score << ")";
            return match_it->second;
        }
    }

    // No match found (exact or fuzzy)
    return std::nullopt;
}

std::size_t MonsterManager::getMonsterCount() const
{
    return impl_->monsters_by_name_.size();
}

std::string MonsterManager::annotateText(const std::string& text) const
{
    if (text.empty())
        return text;
    
    PLOG_DEBUG << "MonsterManager: Annotating text: " << text;
    
    processing::NFKCTextNormalizer normalizer;
    std::u32string u32text = processing::utf8ToUtf32(text);
    std::u32string result;
    result.reserve(u32text.size() * 2);
    
    int match_count = 0;
    size_t pos = 0;
    while (pos < u32text.size())
    {
        bool found_match = false;
        
        for (size_t len = std::min<size_t>(20, u32text.size() - pos); len >= 3; --len)
        {
            std::u32string candidate(u32text.begin() + pos, u32text.begin() + pos + len);
            std::string candidate_utf8 = processing::utf32ToUtf8(candidate);
            std::string normalized_candidate = normalizer.normalize(candidate_utf8);
            
            auto monster_info = findMonsterByName(normalized_candidate);
            
            if (monster_info.has_value())
            {
                bool accept = true;
                if (processing::isPureKatakana(candidate))
                {
                    bool left_ok = (pos == 0) || !processing::isKatakanaChar(u32text[pos - 1]);
                    bool right_ok = (pos + len >= u32text.size()) || !processing::isKatakanaChar(u32text[pos + len]);
                    accept = left_ok && right_ok;
                    if (!accept)
                    {
                        std::u32string prev_s, next_s;
                        if (pos > 0) prev_s.push_back(u32text[pos - 1]);
                        if (pos + len < u32text.size()) next_s.push_back(u32text[pos + len]);
                        PLOG_DEBUG << "MonsterManager: Skip candidate due to katakana boundary: '" << candidate_utf8
                                   << "' prev='" << processing::utf32ToUtf8(prev_s) << "' next='" << processing::utf32ToUtf8(next_s) << "'";
                    }
                }

                if (!accept)
                    continue;

                PLOG_DEBUG << "MonsterManager: Matched monster '" << candidate_utf8 
                          << "' (normalized: '" << normalized_candidate << "') -> ID " << monster_info->id;
                
                result.push_back(processing::MARKER_START);
                std::u32string id_u32 = processing::utf8ToUtf32(monster_info->id);
                result.append(id_u32);
                result.push_back(processing::MARKER_SEP);
                result.append(candidate);
                result.push_back(processing::MARKER_END);
                
                pos += len;
                found_match = true;
                match_count++;
                break;
            }
        }
        
        if (!found_match)
        {
            result.push_back(u32text[pos]);
            ++pos;
        }
    }
    
    if (match_count > 0)
    {
        PLOG_INFO << "MonsterManager: Annotated " << match_count << " monster(s) in text";
    }
    
    return processing::utf32ToUtf8(result);
}
