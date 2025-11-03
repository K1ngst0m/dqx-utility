#pragma once

#include "IFuzzyMatcher.hpp"
#include "ITextNormalizer.hpp"
#include <memory>

namespace processing
{

/**
 * @brief Japanese-specific fuzzy string matcher using NFKC normalization.
 *
 * This implementation:
 * - Normalizes text using NFKC (half/full-width variants, compatibility characters)
 * - Wraps rapidfuzz-cpp algorithms for efficient string matching
 * - Converts rapidfuzz scores (0-100) to normalized scores (0.0-1.0)
 * - Uses cached scoring patterns for multiple candidate comparisons
 *
 * Example:
 * @code
 * JapaneseFuzzyMatcher matcher;
 * double score = matcher.similarity("カタカナ", "ｶﾀｶﾅ"); // High score (normalized)
 * @endcode
 */
class JapaneseFuzzyMatcher : public IFuzzyMatcher
{
public:
    JapaneseFuzzyMatcher();
    ~JapaneseFuzzyMatcher() override;

    std::optional<MatchResult> findBestMatch(const std::string& query, const std::vector<std::string>& candidates,
                                              double threshold,
                                              MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const override;

    std::vector<MatchResult> findMatches(const std::string& query, const std::vector<std::string>& candidates,
                                          double threshold,
                                          MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const override;

    double similarity(const std::string& s1, const std::string& s2,
                      MatchAlgorithm algorithm = MatchAlgorithm::Ratio) const override;

private:
    std::unique_ptr<ITextNormalizer> normalizer_;

    /**
     * @brief Call the appropriate rapidfuzz algorithm and normalize score to [0.0, 1.0].
     */
    double callRapidfuzzAlgorithm(const std::string& s1, const std::string& s2, MatchAlgorithm algorithm) const;
};

} // namespace processing

