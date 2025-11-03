#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "processing/JapaneseFuzzyMatcher.hpp"

using namespace processing;
using Catch::Matchers::WithinAbs;

TEST_CASE("JapaneseFuzzyMatcher - Exact Matches", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Identical strings return score of 1.0")
    {
        double score = matcher.similarity("これはテストです", "これはテストです", MatchAlgorithm::Ratio);
        REQUIRE_THAT(score, WithinAbs(1.0, 0.001));
    }

    SECTION("Identical strings with all algorithms")
    {
        std::string text = "主人公の冒険が始まる";

        REQUIRE_THAT(matcher.similarity(text, text, MatchAlgorithm::Ratio), WithinAbs(1.0, 0.001));
        REQUIRE_THAT(matcher.similarity(text, text, MatchAlgorithm::PartialRatio), WithinAbs(1.0, 0.001));
        REQUIRE_THAT(matcher.similarity(text, text, MatchAlgorithm::TokenSortRatio), WithinAbs(1.0, 0.001));
        REQUIRE_THAT(matcher.similarity(text, text, MatchAlgorithm::TokenSetRatio), WithinAbs(1.0, 0.001));
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Half/Full-Width Normalization", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Half-width katakana matches full-width katakana")
    {
        // ｶﾀｶﾅ (half-width) vs カタカナ (full-width)
        double score = matcher.similarity("ｶﾀｶﾅ", "カタカナ", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.95); // Should be very high after NFKC normalization
    }

    SECTION("Full-width ASCII matches ASCII")
    {
        // ＡＢＣ (full-width) vs ABC (normal)
        double score = matcher.similarity("ＡＢＣ", "ABC", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.95);
    }

    SECTION("Full-width numbers match ASCII numbers")
    {
        // １２３ (full-width) vs 123 (normal)
        double score = matcher.similarity("１２３", "123", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.95);
    }

    SECTION("Mixed half/full-width in sentence")
    {
        double score =
            matcher.similarity("ｺﾚﾊﾃｽﾄです", "コレハテストです", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.90);
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Similar Strings", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Slightly different strings have lower scores")
    {
        double score = matcher.similarity("これはテストです", "あれはテストです", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.7);
        REQUIRE(score < 1.0);
    }

    SECTION("Very different strings have low scores")
    {
        double score = matcher.similarity("完全に違う文章です", "これはテストです", MatchAlgorithm::Ratio);
        REQUIRE(score < 0.5);
    }

    SECTION("Minor typo in character name")
    {
        // エステラ (Estelle) vs エスデラ (typo)
        double score = matcher.similarity("エステラ", "エスデラ", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.7);
        REQUIRE(score < 1.0);
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Empty String Handling", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Empty query string returns 0.0")
    {
        double score = matcher.similarity("", "テスト", MatchAlgorithm::Ratio);
        REQUIRE_THAT(score, WithinAbs(0.0, 0.001));
    }

    SECTION("Empty candidate string returns 0.0")
    {
        double score = matcher.similarity("テスト", "", MatchAlgorithm::Ratio);
        REQUIRE_THAT(score, WithinAbs(0.0, 0.001));
    }

    SECTION("Both empty strings return 0.0")
    {
        double score = matcher.similarity("", "", MatchAlgorithm::Ratio);
        REQUIRE_THAT(score, WithinAbs(0.0, 0.001));
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Algorithm Comparison", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("PartialRatio - substring matching")
    {
        // "テスト" should match "これはテストです" with high partial ratio
        double partial = matcher.similarity("テスト", "これはテストです", MatchAlgorithm::PartialRatio);
        double regular = matcher.similarity("テスト", "これはテストです", MatchAlgorithm::Ratio);

        REQUIRE(partial > regular); // Partial should score higher for substring
        REQUIRE(partial > 0.9);     // Very high score for exact substring
    }

    SECTION("TokenSortRatio - order independence")
    {
        // Word order shouldn't matter
        double score =
            matcher.similarity("冒険者 勇者 戦士", "戦士 冒険者 勇者", MatchAlgorithm::TokenSortRatio);
        REQUIRE(score > 0.95); // Should be very high despite different order
    }

    SECTION("TokenSetRatio - duplicate handling")
    {
        // Duplicates shouldn't affect score much
        double score =
            matcher.similarity("勇者 勇者 冒険", "勇者 冒険", MatchAlgorithm::TokenSetRatio);
        REQUIRE(score > 0.9); // Should handle duplicates well
    }
}

TEST_CASE("JapaneseFuzzyMatcher - findBestMatch", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Finds best match above threshold")
    {
        std::vector<std::string> candidates = {"主人公", "エステラ", "メレアーデ", "アンルシア"};

        auto result = matcher.findBestMatch("エステラ", candidates, 0.5, MatchAlgorithm::Ratio);

        REQUIRE(result.has_value());
        REQUIRE(result->matched == "エステラ");
        REQUIRE_THAT(result->score, WithinAbs(1.0, 0.001));
        REQUIRE(result->algorithm == MatchAlgorithm::Ratio);
    }

    SECTION("Returns nullopt when no match above threshold")
    {
        std::vector<std::string> candidates = {"主人公", "エステラ", "メレアーデ"};

        auto result = matcher.findBestMatch("完全に違う名前", candidates, 0.9, MatchAlgorithm::Ratio);

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Fuzzy match with typo")
    {
        std::vector<std::string> glossary = {"主人公", "エステラ", "メレアーデ", "アンルシア"};

        // エスデラ is a typo of エステラ
        auto result = matcher.findBestMatch("エスデラ", glossary, 0.7, MatchAlgorithm::Ratio);

        REQUIRE(result.has_value());
        REQUIRE(result->matched == "エステラ");
        REQUIRE(result->score > 0.7);
    }

    SECTION("Returns nullopt for empty candidates")
    {
        std::vector<std::string> empty;
        auto result = matcher.findBestMatch("テスト", empty, 0.5, MatchAlgorithm::Ratio);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt for empty query")
    {
        std::vector<std::string> candidates = {"テスト"};
        auto result = matcher.findBestMatch("", candidates, 0.5, MatchAlgorithm::Ratio);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("JapaneseFuzzyMatcher - findMatches", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Finds multiple matches above threshold")
    {
        std::vector<std::string> candidates = {
            "これはテストです",   // "This is a test"
            "あれはテストです",   // "That is a test"
            "完全に違う文章です"  // "Completely different"
        };

        auto matches = matcher.findMatches("これはテストです", candidates, 0.7, MatchAlgorithm::Ratio);

        REQUIRE(matches.size() >= 2);                       // At least first two should match
        REQUIRE(matches[0].matched == "これはテストです");  // Best match should be first
        REQUIRE(matches[0].score > matches[1].score);       // Sorted by score descending
    }

    SECTION("Returns empty vector when no matches")
    {
        std::vector<std::string> candidates = {"完全に", "違う", "文章"};

        auto matches = matcher.findMatches("これはテスト", candidates, 0.9, MatchAlgorithm::Ratio);

        REQUIRE(matches.empty());
    }

    SECTION("Returns all exact matches")
    {
        std::vector<std::string> candidates = {"テスト", "テスト", "テスト"};

        auto matches = matcher.findMatches("テスト", candidates, 0.99, MatchAlgorithm::Ratio);

        REQUIRE(matches.size() == 3);
        for (const auto& match : matches)
        {
            REQUIRE_THAT(match.score, WithinAbs(1.0, 0.001));
        }
    }

    SECTION("Results are sorted by score descending")
    {
        std::vector<std::string> candidates = {"これは", "これはテスト", "これはテストです", "テストです"};

        auto matches = matcher.findMatches("これはテストです", candidates, 0.5, MatchAlgorithm::Ratio);

        REQUIRE(matches.size() >= 2);
        for (size_t i = 0; i < matches.size() - 1; ++i)
        {
            REQUIRE(matches[i].score >= matches[i + 1].score);
        }
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Real-World Patterns", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Character names with variations")
    {
        std::vector<std::string> names = {"アンルシア", "主人公", "リィン", "ヒューザ"};

        // Slight variation
        auto result = matcher.findBestMatch("アンルシヤ", names, 0.8, MatchAlgorithm::Ratio);
        REQUIRE(result.has_value());
        REQUIRE(result->matched == "アンルシア");
    }

    SECTION("Dialog text with punctuation differences")
    {
        double score = matcher.similarity("こんにちは！", "こんにちは", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.8); // Should still be high despite punctuation
    }

    SECTION("Quest text with formatting variations")
    {
        // Different spacing
        double score =
            matcher.similarity("世界樹の葉を５個手に入れた", "世界樹の葉を５個　手に入れた", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.9);
    }

    SECTION("Long dialog sequences")
    {
        std::string dialog1 =
            "主人公は静かに頷いた。そして、深い森の奥へと進んでいった。";
        std::string dialog2 =
            "主人公は静かに頷く。そして、深い森の奥へと進んでいく。";

        double score = matcher.similarity(dialog1, dialog2, MatchAlgorithm::Ratio);
        REQUIRE(score > 0.8); // Similar but not identical
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Score Normalization", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Scores are in range [0.0, 1.0]")
    {
        std::vector<std::string> test_cases = {"テスト", "これはテスト", "完全に違う文章", "ｶﾀｶﾅ"};

        for (const auto& candidate : test_cases)
        {
            double score = matcher.similarity("テスト", candidate, MatchAlgorithm::Ratio);
            REQUIRE(score >= 0.0);
            REQUIRE(score <= 1.0);
        }
    }

    SECTION("All algorithms return normalized scores")
    {
        std::string s1 = "テスト文字列";
        std::string s2 = "テスト";

        std::vector<MatchAlgorithm> algorithms = {MatchAlgorithm::Ratio, MatchAlgorithm::PartialRatio,
                                                   MatchAlgorithm::TokenSortRatio, MatchAlgorithm::TokenSetRatio};

        for (auto algo : algorithms)
        {
            double score = matcher.similarity(s1, s2, algo);
            REQUIRE(score >= 0.0);
            REQUIRE(score <= 1.0);
        }
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Threshold Filtering", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("findBestMatch respects threshold exactly")
    {
        std::vector<std::string> candidates = {"テスト1", "テスト2"};

        // First check what the actual score is
        double actual_score = matcher.similarity("テスト", "テスト1", MatchAlgorithm::Ratio);

        // Setting threshold just above actual score should return nullopt
        auto result_above = matcher.findBestMatch("テスト", candidates, actual_score + 0.01, MatchAlgorithm::Ratio);
        REQUIRE_FALSE(result_above.has_value());

        // Setting threshold at or below actual score should return match
        auto result_at = matcher.findBestMatch("テスト", candidates, actual_score, MatchAlgorithm::Ratio);
        REQUIRE(result_at.has_value());
    }

    SECTION("findMatches respects threshold")
    {
        std::vector<std::string> candidates = {"これはテスト", "あれはテスト", "完全に違う"};

        // High threshold should return fewer results
        auto high_thresh = matcher.findMatches("これはテストです", candidates, 0.95, MatchAlgorithm::Ratio);
        auto low_thresh = matcher.findMatches("これはテストです", candidates, 0.5, MatchAlgorithm::Ratio);

        REQUIRE(high_thresh.size() <= low_thresh.size());
    }
}

TEST_CASE("JapaneseFuzzyMatcher - Edge Cases with Special Characters", "[fuzzy][japanese]")
{
    JapaneseFuzzyMatcher matcher;

    SECTION("Handles newlines")
    {
        double score = matcher.similarity("テスト\nテスト", "テストテスト", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.8);
    }

    SECTION("Handles special punctuation")
    {
        double score = matcher.similarity("テスト！？", "テスト!?", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.8); // NFKC should normalize punctuation
    }

    SECTION("Handles ideographic space")
    {
        // 　(ideographic space) should normalize to regular space
        double score = matcher.similarity("テスト　テスト", "テスト テスト", MatchAlgorithm::Ratio);
        REQUIRE(score > 0.95);
    }
}

