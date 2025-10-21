#pragma once

#include <string>
#include <unordered_map>
#include <optional>

namespace processing
{

class GlossaryManager
{
public:
    GlossaryManager() = default;

    void initialize();

    std::optional<std::string> lookup(const std::string& japanese_text, const std::string& target_lang) const;

    bool hasGlossary(const std::string& target_lang) const;

    std::size_t getEntryCount(const std::string& target_lang) const;

    bool isInitialized() const;

private:
    bool loadGlossaryFile(const std::string& file_path, const std::string& language_code);

    std::string mapToGlossaryLanguage(const std::string& target_lang) const;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> glossaries_;
    bool initialized_ = false;
};

} // namespace processing
