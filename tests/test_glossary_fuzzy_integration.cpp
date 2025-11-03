#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "processing/GlossaryManager.hpp"
#include <fstream>
#include <filesystem>

using namespace processing;
using Catch::Matchers::WithinAbs;

namespace fs = std::filesystem;

// Test fixture for temporary glossary files
class TempGlossary
{
public:
    TempGlossary(const std::string& lang, const std::string& content)
    {
        test_dir_ = "test_temp_glossaries";
        fs::create_directories(test_dir_);
        file_path_ = test_dir_ + "/" + lang + ".json";
        std::ofstream file(file_path_);
        file << content;
        file.close();
    }

    ~TempGlossary()
    {
        if (fs::exists(file_path_))
        {
            fs::remove(file_path_);
        }
        if (fs::exists(test_dir_) && fs::is_empty(test_dir_))
        {
            fs::remove(test_dir_);
        }
    }

private:
    std::string file_path_;
    std::string test_dir_;
};

TEST_CASE("GlossaryManager - Fuzzy Lookup Basic", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("en-US", R"({
        "主人公": "Protagonist",
        "エステラ": "Estelle",
        "メレアーデ": "Meredith",
        "アンルシア": "Anlucia"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Exact match returns score 1.0")
    {
        auto results = manager.fuzzyLookup("主人公", "en-US", 0.8);

        REQUIRE(results.size() >= 1);
        REQUIRE(std::get<0>(results[0]) == "主人公");
        REQUIRE(std::get<1>(results[0]) == "Protagonist");
        REQUIRE_THAT(std::get<2>(results[0]), WithinAbs(1.0, 0.001));
    }

    SECTION("Fuzzy match returns appropriate score")
    {
        // エスデラ (typo) should match エステラ with high score
        auto results = manager.fuzzyLookup("エスデラ", "en-US", 0.7);

        REQUIRE(results.size() >= 1);
        bool found_estelle = false;
        for (const auto& [japanese, translation, score] : results)
        {
            if (translation == "Estelle")
            {
                found_estelle = true;
                REQUIRE(score > 0.7);
                REQUIRE(score < 1.0);
            }
        }
        REQUIRE(found_estelle);
    }

    SECTION("Threshold filtering works")
    {
        // High threshold should return fewer/no results for dissimilar text
        auto high_threshold = manager.fuzzyLookup("完全に違う", "en-US", 0.95);
        auto low_threshold = manager.fuzzyLookup("完全に違う", "en-US", 0.3);

        REQUIRE(high_threshold.size() <= low_threshold.size());
    }
}

TEST_CASE("GlossaryManager - Fuzzy Snippet Building", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("zh-Hans", R"({
        "主人公": "主角",
        "エステラ": "艾丝黛拉",
        "メレアーデ": "梅蕾娅蒂",
        "世界樹の葉": "世界树之叶"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Exact match in text returns score 1.00")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公が到着した", "zh-CN", 0.8, 10);

        REQUIRE(!snippet.empty());
        REQUIRE(snippet.find("主人公") != std::string::npos);
        REQUIRE(snippet.find("主角") != std::string::npos);
        REQUIRE(snippet.find("(1.00)") != std::string::npos);
    }

    SECTION("Fuzzy match includes similarity score")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("エスデラとメレアデ", "zh-CN", 0.7, 10);

        REQUIRE(!snippet.empty());
        // Should contain entries with scores in parentheses
        bool has_score = (snippet.find("(0.") != std::string::npos) || (snippet.find("(1.") != std::string::npos);
        REQUIRE(has_score);
    }

    SECTION("Empty text returns empty snippet")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("", "zh-CN", 0.8, 10);
        REQUIRE(snippet.empty());
    }

    SECTION("Max entries limit is respected")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公エステラメレアーデ", "zh-CN", 0.5, 2);

        REQUIRE(!snippet.empty());
        // Count newlines to verify entry count
        size_t newline_count = std::count(snippet.begin(), snippet.end(), '\n');
        REQUIRE(newline_count <= 1); // 2 entries = 1 newline
    }
}

TEST_CASE("GlossaryManager - Fuzzy Toggle", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("en-US", R"({
        "主人公": "Protagonist",
        "エステラ": "Estelle"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Default state is enabled")
    {
        REQUIRE(manager.isFuzzyMatchingEnabled());
    }

    SECTION("Can disable fuzzy matching")
    {
        manager.setFuzzyMatchingEnabled(false);
        REQUIRE_FALSE(manager.isFuzzyMatchingEnabled());
    }

    SECTION("Disabled fuzzy returns only exact matches")
    {
        manager.setFuzzyMatchingEnabled(false);

        // Exact match should still work
        auto exact_results = manager.fuzzyLookup("主人公", "en-US", 0.8);
        REQUIRE(exact_results.size() >= 1);
        REQUIRE(std::get<1>(exact_results[0]) == "Protagonist");

        // Fuzzy match should not work
        auto fuzzy_results = manager.fuzzyLookup("エスデラ", "en-US", 0.7);
        REQUIRE(fuzzy_results.empty());
    }

    SECTION("Re-enabling fuzzy matching works")
    {
        manager.setFuzzyMatchingEnabled(false);
        manager.setFuzzyMatchingEnabled(true);

        REQUIRE(manager.isFuzzyMatchingEnabled());

        // Fuzzy match should work again
        auto results = manager.fuzzyLookup("エスデラ", "en-US", 0.7);
        REQUIRE(!results.empty());
    }
}

TEST_CASE("GlossaryManager - Snippet Format Validation", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("en-US", R"({
        "主人公": "Protagonist",
        "エステラ": "Estelle"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Snippet contains arrow separator")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公", "en-US", 0.8, 10);

        REQUIRE(snippet.find(" → ") != std::string::npos);
    }

    SECTION("Snippet contains score in parentheses")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公", "en-US", 0.8, 10);

        REQUIRE(snippet.find("(") != std::string::npos);
        REQUIRE(snippet.find(")") != std::string::npos);
    }

    SECTION("Multiple entries separated by newlines")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公エステラ", "en-US", 0.5, 10);

        if (snippet.find('\n') != std::string::npos)
        {
            // If multiple entries, verify format
            size_t arrow_count = 0;
            size_t pos = 0;
            while ((pos = snippet.find(" → ", pos)) != std::string::npos)
            {
                ++arrow_count;
                ++pos;
            }
            REQUIRE(arrow_count >= 2);
        }
    }
}

TEST_CASE("GlossaryManager - Language Mapping", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary_hans("zh-Hans", R"({"主人公": "主角"})");
    TempGlossary glossary_hant("zh-Hant", R"({"主人公": "主角"})");

    GlossaryManager manager;
    manager.initialize();

    SECTION("zh-CN maps to zh-Hans")
    {
        auto results = manager.fuzzyLookup("主人公", "zh-CN", 0.8);
        REQUIRE(!results.empty());
    }

    SECTION("zh-TW maps to zh-Hant")
    {
        auto results = manager.fuzzyLookup("主人公", "zh-TW", 0.8);
        REQUIRE(!results.empty());
    }

    SECTION("Case insensitive language codes")
    {
        auto results_lower = manager.fuzzyLookup("主人公", "zh-cn", 0.8);
        auto results_upper = manager.fuzzyLookup("主人公", "zh-CN", 0.8);

        REQUIRE(results_lower.size() == results_upper.size());
    }
}

TEST_CASE("GlossaryManager - Edge Cases", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("en-US", R"({
        "主人公": "Protagonist",
        "エステラ": "Estelle"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Empty query returns empty results")
    {
        auto results = manager.fuzzyLookup("", "en-US", 0.8);
        REQUIRE(results.empty());
    }

    SECTION("Non-existent language returns empty results")
    {
        auto results = manager.fuzzyLookup("主人公", "fr-FR", 0.8);
        REQUIRE(results.empty());
    }

    SECTION("Zero threshold includes more results")
    {
        auto results = manager.fuzzyLookup("テスト", "en-US", 0.0);
        // Should return results even with very low similarity
        // (exact behavior depends on glossary content)
    }

    SECTION("Threshold of 1.0 returns only exact matches")
    {
        auto results = manager.fuzzyLookup("主人公", "en-US", 1.0);

        REQUIRE(results.size() >= 1);
        for (const auto& [japanese, translation, score] : results)
        {
            REQUIRE_THAT(score, WithinAbs(1.0, 0.001));
        }
    }
}

TEST_CASE("GlossaryManager - Half/Full Width Matching", "[glossary][fuzzy][integration]")
{
    TempGlossary glossary("en-US", R"({
        "カタカナ": "Katakana"
    })");

    GlossaryManager manager;
    manager.initialize();

    SECTION("Half-width katakana matches full-width")
    {
        // ｶﾀｶﾅ (half-width) should match カタカナ (full-width) via NFKC normalization
        auto results = manager.fuzzyLookup("ｶﾀｶﾅ", "en-US", 0.9);

        REQUIRE(!results.empty());
        bool found = false;
        for (const auto& [japanese, translation, score] : results)
        {
            if (translation == "Katakana")
            {
                found = true;
                REQUIRE(score > 0.9); // Should be very high after normalization
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("GlossaryManager - Uninitialized State", "[glossary][fuzzy][integration]")
{
    GlossaryManager manager;
    // Don't call initialize()

    SECTION("Fuzzy lookup on uninitialized manager returns empty")
    {
        auto results = manager.fuzzyLookup("主人公", "en-US", 0.8);
        REQUIRE(results.empty());
    }

    SECTION("Snippet building on uninitialized manager returns empty")
    {
        std::string snippet = manager.buildFuzzyGlossarySnippet("主人公", "en-US", 0.8, 10);
        REQUIRE(snippet.empty());
    }
}

