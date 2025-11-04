#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <cstddef>
#include <memory>
#include <vector>
#include <utility>

namespace processing
{

class IFuzzyMatcher;

class GlossaryManager
{
public:
    GlossaryManager();
    ~GlossaryManager();

    // Explicitly delete copy/move to avoid issues with unique_ptr of incomplete type
    GlossaryManager(const GlossaryManager&) = delete;
    GlossaryManager& operator=(const GlossaryManager&) = delete;
    GlossaryManager(GlossaryManager&&) = delete;
    GlossaryManager& operator=(GlossaryManager&&) = delete;

    void initialize(const std::string& glossary_dir = "assets/glossaries");

    std::optional<std::string> lookup(const std::string& japanese_text, const std::string& target_lang) const;

    bool hasGlossary(const std::string& target_lang) const;

    std::size_t getEntryCount(const std::string& target_lang) const;

    bool isInitialized() const;

    std::string buildGlossarySnippet(const std::string& text, const std::string& target_lang,
                                     std::size_t max_entries = 10) const;

    /**
     * @brief Perform fuzzy lookup for similar glossary terms.
     *
     * @param japanese_text The Japanese text to match against glossary terms
     * @param target_lang Target language for translation
     * @param threshold Minimum similarity score (0.0-1.0) to include results
     * @return Vector of pairs: (original_japanese_term, translation, similarity_score)
     */
    std::vector<std::tuple<std::string, std::string, double>> fuzzyLookup(const std::string& japanese_text,
                                                                            const std::string& target_lang,
                                                                            double threshold = 0.8) const;

    /**
     * @brief Build glossary snippet with fuzzy matching and similarity scores.
     *
     * @param text The full Japanese text to search for glossary matches
     * @param target_lang Target language for translations
     * @param threshold Minimum similarity score (0.0-1.0) to include results
     * @param max_entries Maximum number of entries to include
     * @return Formatted glossary snippet with scores: "主人公 → Protagonist (1.00)\n..."
     */
    std::string buildFuzzyGlossarySnippet(const std::string& text, const std::string& target_lang,
                                           double threshold = 0.8, std::size_t max_entries = 10) const;

    /**
     * @brief Enable or disable fuzzy matching feature.
     */
    void setFuzzyMatchingEnabled(bool enabled);

    /**
     * @brief Check if fuzzy matching is enabled.
     */
    bool isFuzzyMatchingEnabled() const;

private:
    bool loadGlossaryFile(const std::string& file_path, const std::string& language_code);

    std::string mapToGlossaryLanguage(const std::string& target_lang) const;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> glossaries_;
    std::unique_ptr<IFuzzyMatcher> fuzzy_matcher_;
    bool initialized_ = false;
    bool fuzzy_matching_enabled_ = true;
};

} // namespace processing
