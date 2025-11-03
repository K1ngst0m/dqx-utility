#include "JapaneseFuzzyMatcher.hpp"
#include "NFKCTextNormalizer.hpp"
#include <rapidfuzz/fuzz.hpp>
#include <algorithm>

namespace processing
{

JapaneseFuzzyMatcher::JapaneseFuzzyMatcher() : normalizer_(std::make_unique<NFKCTextNormalizer>())
{
}

JapaneseFuzzyMatcher::~JapaneseFuzzyMatcher() = default;

std::optional<MatchResult> JapaneseFuzzyMatcher::findBestMatch(const std::string& query,
                                                                 const std::vector<std::string>& candidates,
                                                                 double threshold, MatchAlgorithm algorithm) const
{
    if (candidates.empty() || query.empty())
    {
        return std::nullopt;
    }

    // Normalize query once
    std::string normalized_query = normalizer_->normalize(query);

    double best_score = threshold;
    std::string best_match;
    bool match_found = false;

    // Find best match among all candidates
    for (const auto& candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        std::string normalized_candidate = normalizer_->normalize(candidate);
        double score = callRapidfuzzAlgorithm(normalized_query, normalized_candidate, algorithm);

        if (score >= best_score)
        {
            best_score = score;
            best_match = candidate; // Store original (non-normalized) text
            match_found = true;
        }
    }

    if (!match_found)
    {
        return std::nullopt;
    }

    return MatchResult{best_score, best_match, algorithm};
}

std::vector<MatchResult> JapaneseFuzzyMatcher::findMatches(const std::string& query,
                                                             const std::vector<std::string>& candidates,
                                                             double threshold, MatchAlgorithm algorithm) const
{
    std::vector<MatchResult> results;

    if (candidates.empty() || query.empty())
    {
        return results;
    }

    // Normalize query once
    std::string normalized_query = normalizer_->normalize(query);

    // Find all matches above threshold
    for (const auto& candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        std::string normalized_candidate = normalizer_->normalize(candidate);
        double score = callRapidfuzzAlgorithm(normalized_query, normalized_candidate, algorithm);

        if (score >= threshold)
        {
            results.push_back(MatchResult{score, candidate, algorithm}); // Store original text
        }
    }

    // Sort by score (descending)
    std::sort(results.begin(), results.end(),
              [](const MatchResult& a, const MatchResult& b) { return a.score > b.score; });

    return results;
}

double JapaneseFuzzyMatcher::similarity(const std::string& s1, const std::string& s2, MatchAlgorithm algorithm) const
{
    if (s1.empty() || s2.empty())
    {
        return 0.0;
    }

    // Normalize both strings
    std::string normalized_s1 = normalizer_->normalize(s1);
    std::string normalized_s2 = normalizer_->normalize(s2);

    return callRapidfuzzAlgorithm(normalized_s1, normalized_s2, algorithm);
}

double JapaneseFuzzyMatcher::callRapidfuzzAlgorithm(const std::string& s1, const std::string& s2,
                                                     MatchAlgorithm algorithm) const
{
    double rapidfuzz_score = 0.0;

    switch (algorithm)
    {
    case MatchAlgorithm::Ratio:
        rapidfuzz_score = rapidfuzz::fuzz::ratio(s1, s2);
        break;

    case MatchAlgorithm::PartialRatio:
        rapidfuzz_score = rapidfuzz::fuzz::partial_ratio(s1, s2);
        break;

    case MatchAlgorithm::TokenSortRatio:
        rapidfuzz_score = rapidfuzz::fuzz::token_sort_ratio(s1, s2);
        break;

    case MatchAlgorithm::TokenSetRatio:
        rapidfuzz_score = rapidfuzz::fuzz::token_set_ratio(s1, s2);
        break;

    default:
        // Fallback to simple ratio
        rapidfuzz_score = rapidfuzz::fuzz::ratio(s1, s2);
        break;
    }

    // Normalize from [0, 100] to [0.0, 1.0]
    return rapidfuzz_score / 100.0;
}

} // namespace processing

