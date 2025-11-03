#pragma once

#include <optional>
#include <string>
#include <vector>

namespace processing
{

/**
 * @brief Fuzzy matching algorithms supported by the matcher.
 */
enum class MatchAlgorithm
{
    Ratio,          // Simple Levenshtein-based ratio (general purpose)
    PartialRatio,   // Partial substring matching (e.g., "test" matches "this is a test")
    TokenSortRatio, // Order-independent token matching (e.g., "A B" matches "B A")
    TokenSetRatio   // Set-based token matching (handles duplicates: "A A B" matches "A B")
};

/**
 * @brief Result of a fuzzy matching operation.
 */
struct MatchResult
{
    double score;              // Similarity score normalized to [0.0, 1.0]
    std::string matched;       // The original candidate text that was matched
    MatchAlgorithm algorithm;  // The algorithm used for matching
};

/**
 * @brief Abstract interface for language-specific fuzzy string matchers.
 *
 * Provides unified API for fuzzy text matching with support for multiple algorithms.
 * Implementations should handle language-specific normalization (e.g., NFKC for Japanese).
 *
 * Typical use cases:
 * - Fuzzy glossary lookups (find similar terms despite variations)
 * - Duplicate text detection (identify similar dialog/quest text)
 * - Text similarity scoring for general purposes
 */
class IFuzzyMatcher
{
public:
    virtual ~IFuzzyMatcher() = default;

    /**
     * @brief Find the best matching candidate above the threshold.
     *
     * @param query The query string to match against candidates
     * @param candidates List of candidate strings to search
     * @param threshold Minimum similarity score [0.0, 1.0] required for a match
     * @param algorithm The matching algorithm to use (default: Ratio)
     * @return The best match if score >= threshold, otherwise std::nullopt
     */
    virtual std::optional<MatchResult> findBestMatch(const std::string& query,
                                                      const std::vector<std::string>& candidates, double threshold,
                                                      MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const = 0;

    /**
     * @brief Find all candidates matching above the threshold.
     *
     * @param query The query string to match against candidates
     * @param candidates List of candidate strings to search
     * @param threshold Minimum similarity score [0.0, 1.0] required for a match
     * @param algorithm The matching algorithm to use (default: Ratio)
     * @return Vector of all matches with score >= threshold, sorted by score (descending)
     */
    virtual std::vector<MatchResult> findMatches(const std::string& query,
                                                  const std::vector<std::string>& candidates, double threshold,
                                                  MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const = 0;

    /**
     * @brief Calculate similarity between two strings.
     *
     * @param s1 First string
     * @param s2 Second string
     * @param algorithm The matching algorithm to use (default: Ratio)
     * @return Similarity score normalized to [0.0, 1.0]
     */
    virtual double similarity(const std::string& s1, const std::string& s2,
                              MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const = 0;
};

} // namespace processing

