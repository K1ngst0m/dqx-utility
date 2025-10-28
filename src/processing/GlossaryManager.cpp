#include "GlossaryManager.hpp"
#include "Diagnostics.hpp"

#include <plog/Log.h>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace processing
{

void GlossaryManager::initialize()
{
    if (initialized_)
    {
        PLOG_WARNING_(Diagnostics::kLogInstance) << "[GlossaryManager] Already initialized, skipping";
        return;
    }

    PLOG_INFO_(Diagnostics::kLogInstance) << "[GlossaryManager] Initializing glossaries...";

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

    fs::path glossary_dir = "assets/glossaries";
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

} // namespace processing
