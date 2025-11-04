#include "GlossaryManager.hpp"
#include "Diagnostics.hpp"
#include "JapaneseFuzzyMatcher.hpp"
#include "IFuzzyMatcher.hpp"

#include <plog/Log.h>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace processing
{

GlossaryManager::GlossaryManager()
    : fuzzy_matcher_(std::make_unique<JapaneseFuzzyMatcher>()), fuzzy_matching_enabled_(true)
{
}

GlossaryManager::~GlossaryManager() = default;

void GlossaryManager::initialize(const std::string& glossary_dir_path)
{
    if (initialized_)
    {
        PLOG_WARNING_(Diagnostics::kLogInstance) << "[GlossaryManager] Already initialized, skipping";
        return;
    }

    PLOG_INFO_(Diagnostics::kLogInstance) << "[GlossaryManager] Initializing glossaries from: " << glossary_dir_path;

    // Define glossary files to load
    struct GlossaryFile
    {
        std::string file_name;
        std::string language_code;
    };

    std::vector<GlossaryFile> files = {
        { "zh-Hans.json", "zh-Hans" },
        { "zh-Hant.json", "zh-Hant" },
        { "en-US.json",   "en-US"   }
    };

    fs::path glossary_dir = glossary_dir_path;
    std::size_t total_loaded = 0;
    std::size_t total_entries = 0;

    for (const auto& file : files)
    {
        fs::path file_path = glossary_dir / file.file_name;
        if (loadGlossaryFile(file_path.string(), file.language_code))
        {
            std::size_t count = getEntryCount(file.language_code);
            total_loaded++;
            total_entries += count;
            PLOG_INFO_(Diagnostics::kLogInstance)
                << "[GlossaryManager] Loaded " << file.language_code << " glossary: " << count << " entries";
        }
        else
        {
            PLOG_WARNING_(Diagnostics::kLogInstance)
                << "[GlossaryManager] Failed to load " << file.file_name << " (file may not exist or is empty)";
        }
    }

    initialized_ = true;
    PLOG_INFO_(Diagnostics::kLogInstance) << "[GlossaryManager] Initialization complete: " << total_loaded << " files, "
                                          << total_entries << " total entries";
}

std::optional<std::string> GlossaryManager::lookup(const std::string& japanese_text,
                                                   const std::string& target_lang) const
{
    if (!initialized_)
    {
        return std::nullopt;
    }

    // Map target language to glossary language (zh-CN/zh-TW → zh-Hans)
    std::string glossary_lang = mapToGlossaryLanguage(target_lang);

    auto lang_it = glossaries_.find(glossary_lang);
    if (lang_it == glossaries_.end())
    {
        return std::nullopt;
    }

    const auto& glossary_map = lang_it->second;
    auto entry_it = glossary_map.find(japanese_text);
    if (entry_it == glossary_map.end())
    {
        return std::nullopt;
    }

    return entry_it->second;
}

bool GlossaryManager::hasGlossary(const std::string& target_lang) const
{
    std::string glossary_lang = mapToGlossaryLanguage(target_lang);
    return glossaries_.find(glossary_lang) != glossaries_.end();
}

std::size_t GlossaryManager::getEntryCount(const std::string& target_lang) const
{
    std::string glossary_lang = mapToGlossaryLanguage(target_lang);
    auto it = glossaries_.find(glossary_lang);
    if (it == glossaries_.end())
    {
        return 0;
    }
    return it->second.size();
}

bool GlossaryManager::isInitialized() const { return initialized_; }

std::string GlossaryManager::buildGlossarySnippet(const std::string& text, const std::string& target_lang,
                                                  std::size_t max_entries) const
{
    if (!initialized_ || text.empty() || max_entries == 0)
    {
        return {};
    }

    std::string glossary_lang = mapToGlossaryLanguage(target_lang);
    auto lang_it = glossaries_.find(glossary_lang);
    if (lang_it == glossaries_.end())
    {
        return {};
    }

    std::array<bool, 256> present{};
    for (unsigned char c : text)
    {
        present[c] = true;
    }

    std::vector<std::pair<std::string, std::string>> matches;
    matches.reserve(max_entries);
    const auto& glossary_map = lang_it->second;
    for (const auto& entry : glossary_map)
    {
        const std::string& source = entry.first;
        if (source.empty())
            continue;
        if (!present[static_cast<unsigned char>(source[0])])
            continue;
        if (text.find(source) == std::string::npos)
            continue;
        matches.emplace_back(entry);
        if (matches.size() >= max_entries)
            break;
    }

    if (matches.empty())
        return {};

    std::string snippet;
    snippet.reserve(matches.size() * 24);
    for (const auto& pair : matches)
    {
        snippet.append(pair.first);
        snippet.append(" → ");
        snippet.append(pair.second);
        snippet.push_back('\n');
    }
    if (!snippet.empty())
        snippet.pop_back();
    return snippet;
}

bool GlossaryManager::loadGlossaryFile(const std::string& file_path, const std::string& language_code)
{
    if (!fs::exists(file_path))
    {
        PLOG_DEBUG_(Diagnostics::kLogInstance) << "[GlossaryManager] Glossary file not found: " << file_path;
        return false;
    }

    std::ifstream file(file_path);
    if (!file.is_open())
    {
        PLOG_ERROR_(Diagnostics::kLogInstance) << "[GlossaryManager] Failed to open glossary file: " << file_path;
        return false;
    }

    try
    {
        json j;
        file >> j;

        if (!j.is_object())
        {
            PLOG_ERROR_(Diagnostics::kLogInstance)
                << "[GlossaryManager] Invalid JSON format (expected object): " << file_path;
            return false;
        }

        std::unordered_map<std::string, std::string> glossary_map;
        for (auto& [japanese, translation] : j.items())
        {
            if (translation.is_string())
            {
                glossary_map[japanese] = translation.get<std::string>();
            }
            else
            {
                PLOG_WARNING_(Diagnostics::kLogInstance)
                    << "[GlossaryManager] Skipping non-string translation for key: " << japanese;
            }
        }

        glossaries_[language_code] = std::move(glossary_map);
        return true;
    }
    catch (const json::exception& e)
    {
        PLOG_ERROR_(Diagnostics::kLogInstance)
            << "[GlossaryManager] JSON parse error in " << file_path << ": " << e.what();
        return false;
    }
}

std::string GlossaryManager::mapToGlossaryLanguage(const std::string& target_lang) const
{
    // Both zh-CN (Simplified) and zh-TW (Traditional) use the same zh-Hans glossary
    if (target_lang == "zh-CN" || target_lang == "zh-cn")
    {
        return "zh-Hans";
    }

    if (target_lang == "zh-TW" || target_lang == "zh-tw")
    {
        return "zh-Hant";
    }

    // English uses en-US
    if (target_lang == "en-US" || target_lang == "en-us")
    {
        return "en-US";
    }

    // Default fallback
    return target_lang;
}

std::vector<std::tuple<std::string, std::string, double>>
GlossaryManager::fuzzyLookup(const std::string& japanese_text, const std::string& target_lang, double threshold) const
{
    std::vector<std::tuple<std::string, std::string, double>> results;

    if (!initialized_ || japanese_text.empty() || !fuzzy_matcher_)
    {
        return results;
    }

    std::string glossary_lang = mapToGlossaryLanguage(target_lang);
    auto lang_it = glossaries_.find(glossary_lang);
    if (lang_it == glossaries_.end())
    {
        return results;
    }

    const auto& glossary_map = lang_it->second;

    // 1. Check for exact match first (highest priority)
    auto exact_it = glossary_map.find(japanese_text);
    if (exact_it != glossary_map.end())
    {
        results.emplace_back(exact_it->first, exact_it->second, 1.0);
    }

    // 2. If fuzzy matching is disabled, return only exact match
    if (!fuzzy_matching_enabled_)
    {
        return results;
    }

    // 3. Perform fuzzy matching against all glossary keys
    std::vector<std::string> candidates;
    candidates.reserve(glossary_map.size());
    for (const auto& entry : glossary_map)
    {
        candidates.push_back(entry.first);
    }

    // Find fuzzy matches
    auto fuzzy_matches = fuzzy_matcher_->findMatches(japanese_text, candidates, threshold, MatchAlgorithm::Ratio);

    // 4. Add fuzzy matches (avoid duplicates from exact match)
    std::set<std::string> seen_translations;
    bool had_exact_match = !results.empty();
    if (had_exact_match)
    {
        seen_translations.insert(std::get<1>(results[0])); // Mark exact match translation as seen
    }

    for (const auto& match : fuzzy_matches)
    {
        auto translation_it = glossary_map.find(match.matched);
        if (translation_it != glossary_map.end())
        {
            const std::string& translation = translation_it->second;

            // Skip if we already have this translation from exact match
            if (seen_translations.find(translation) == seen_translations.end())
            {
                // Skip exact match score 1.0 only if we already added an exact match
                if (!had_exact_match || match.score < 0.9999)
                {
                    results.emplace_back(match.matched, translation, match.score);
                    seen_translations.insert(translation);
                }
            }
        }
    }

    // Results are already sorted by score (descending) from fuzzy matcher
    return results;
}

std::string GlossaryManager::buildFuzzyGlossarySnippet(const std::string& text, const std::string& target_lang,
                                                        double threshold, std::size_t max_entries) const
{
    if (!initialized_ || text.empty() || max_entries == 0)
    {
        return {};
    }

    std::string glossary_lang = mapToGlossaryLanguage(target_lang);
    auto lang_it = glossaries_.find(glossary_lang);
    if (lang_it == glossaries_.end())
    {
        return {};
    }

    const auto& glossary_map = lang_it->second;

    // Use character presence optimization for exact matches
    std::array<bool, 256> present{};
    for (unsigned char c : text)
    {
        present[c] = true;
    }

    // Collect all potential matches (exact + fuzzy)
    std::vector<std::tuple<std::string, std::string, double>> all_matches;
    std::set<std::string> matched_terms; // Track which Japanese terms we've already matched

    // 1. Find exact substring matches first
    for (const auto& entry : glossary_map)
    {
        const std::string& source = entry.first;
        if (source.empty())
            continue;
        if (!present[static_cast<unsigned char>(source[0])])
            continue;
        if (text.find(source) == std::string::npos)
            continue;

        all_matches.emplace_back(source, entry.second, 1.0);
        matched_terms.insert(source);

        if (all_matches.size() >= max_entries)
            break;
    }

    // 2. If fuzzy matching is enabled and we haven't reached max_entries, add fuzzy matches
    if (fuzzy_matching_enabled_ && fuzzy_matcher_ && all_matches.size() < max_entries)
    {
        // Extract all glossary keys as candidates
        std::vector<std::string> candidates;
        candidates.reserve(glossary_map.size());
        for (const auto& entry : glossary_map)
        {
            // Skip terms we already matched exactly
            if (matched_terms.find(entry.first) == matched_terms.end())
            {
                candidates.push_back(entry.first);
            }
        }

        // Perform fuzzy matching against the full text
        if (!candidates.empty())
        {
            auto fuzzy_matches =
                fuzzy_matcher_->findMatches(text, candidates, threshold, MatchAlgorithm::PartialRatio);

            for (const auto& match : fuzzy_matches)
            {
                auto translation_it = glossary_map.find(match.matched);
                if (translation_it != glossary_map.end())
                {
                    all_matches.emplace_back(match.matched, translation_it->second, match.score);

                    if (all_matches.size() >= max_entries)
                        break;
                }
            }
        }
    }

    if (all_matches.empty())
        return {};

    // Sort by score descending
    std::sort(all_matches.begin(), all_matches.end(),
              [](const auto& a, const auto& b) { return std::get<2>(a) > std::get<2>(b); });

    // Format snippet with scores
    std::ostringstream snippet;
    snippet << std::fixed << std::setprecision(2);

    std::size_t count = 0;
    for (const auto& [japanese, translation, score] : all_matches)
    {
        if (count >= max_entries)
            break;

        snippet << japanese << " → " << translation << " (" << score << ")";

        if (count < all_matches.size() - 1 && count < max_entries - 1)
        {
            snippet << '\n';
        }

        ++count;
    }

    return snippet.str();
}

void GlossaryManager::setFuzzyMatchingEnabled(bool enabled)
{
    fuzzy_matching_enabled_ = enabled;
    PLOG_INFO_(Diagnostics::kLogInstance)
        << "[GlossaryManager] Fuzzy matching " << (enabled ? "enabled" : "disabled");
}

bool GlossaryManager::isFuzzyMatchingEnabled() const { return fuzzy_matching_enabled_; }

} // namespace processing
