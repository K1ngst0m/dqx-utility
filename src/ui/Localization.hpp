#pragma once

#include <string>
#include <unordered_map>

namespace i18n
{
    // Initialize localization with a language code (e.g., "en", "zh-CN").
    // Loads English as the fallback plus the selected language.
    void init(const std::string& lang_code);

    // Switch language at runtime. Returns true if loaded, false if fallback to en only.
    bool set_language(const std::string& lang_code);

    // Current language code (e.g., "en").
    const std::string& current_language();

    // Lookup localized text by key. Returns the best available string (selected language -> en -> key).
    const std::string& get_str(const std::string& key);

    // Convenience: C-string view for ImGui APIs.
    const char* get(const char* key);

    // Named placeholder formatting: replaces {name} with values provided.
    // Example: format("settings.total", {{"count", "3"}})
    std::string format(const std::string& key,
                       const std::unordered_map<std::string, std::string>& args);
}