#include <catch2/catch_test_macros.hpp>
#include "processing/NFKCTextNormalizer.hpp"
#include <string>

TEST_CASE("NFKCTextNormalizer - normalizeLineEndings converts CRLF to LF", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\r\nLine 2\r\nLine 3";
    std::string result = normalizer.normalizeLineEndings(input);
    REQUIRE(result == "Line 1\nLine 2\nLine 3");
}

TEST_CASE("NFKCTextNormalizer - normalizeLineEndings converts CR to LF", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\rLine 2\rLine 3";
    std::string result = normalizer.normalizeLineEndings(input);
    REQUIRE(result == "Line 1\nLine 2\nLine 3");
}

TEST_CASE("NFKCTextNormalizer - normalizeLineEndings handles mixed line endings", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\r\nLine 2\nLine 3\rLine 4";
    std::string result = normalizer.normalizeLineEndings(input);
    REQUIRE(result == "Line 1\nLine 2\nLine 3\nLine 4");
}

TEST_CASE("NFKCTextNormalizer - normalizeLineEndings preserves Japanese text", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "こんにちは\r\n世界";
    std::string result = normalizer.normalizeLineEndings(input);
    REQUIRE(result == "こんにちは\n世界");
}

TEST_CASE("NFKCTextNormalizer - normalizeLineEndings handles empty string", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "";
    std::string result = normalizer.normalizeLineEndings(input);
    REQUIRE(result == "");
}

TEST_CASE("NFKCTextNormalizer - collapseNewlines allows up to 2 consecutive newlines", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\n\nLine 2";
    std::string result = normalizer.collapseNewlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("NFKCTextNormalizer - collapseNewlines reduces 3+ newlines to 2", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\n\n\n\n\nLine 2";
    std::string result = normalizer.collapseNewlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("NFKCTextNormalizer - collapseNewlines handles Japanese text", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "最初の行\n\n\n\n二番目の行";
    std::string result = normalizer.collapseNewlines(input);
    REQUIRE(result == "最初の行\n\n二番目の行");
}

TEST_CASE("NFKCTextNormalizer - collapseNewlines handles dialog with quotes", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "「ようこそ！」\n\n\n\n「冒険者よ、この村へようこそ！」";
    std::string result = normalizer.collapseNewlines(input);
    REQUIRE(result == "「ようこそ！」\n\n「冒険者よ、この村へようこそ！」");
}

TEST_CASE("NFKCTextNormalizer - collapseNewlines handles empty string", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "";
    std::string result = normalizer.collapseNewlines(input);
    REQUIRE(result == "");
}

TEST_CASE("NFKCTextNormalizer - normalize performs full pipeline", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Line 1\r\n\r\n\r\n\r\nLine 2";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("NFKCTextNormalizer - normalize converts half-width katakana to full-width", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ｶﾀｶﾅ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "カタカナ");
}

TEST_CASE("NFKCTextNormalizer - normalize handles compatibility characters", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "㌫";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "パーセント");
}

TEST_CASE("NFKCTextNormalizer - normalize handles ㍻ (Heisei era)", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "㍻";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "平成");
}

TEST_CASE("NFKCTextNormalizer - normalize handles mixed hiragana and katakana", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ひらがな と ｶﾀｶﾅ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "ひらがな と カタカナ");
}

TEST_CASE("NFKCTextNormalizer - normalize handles Japanese dialog patterns", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "「勇者よ、ようこそ！」\r\n\r\n\r\n「この村へ来てくれてありがとう」";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "「勇者よ、ようこそ！」\n\n「この村へ来てくれてありがとう」");
}

TEST_CASE("NFKCTextNormalizer - normalize handles kanji with furigana patterns", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "勇者（ゆうしゃ）";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "勇者（ゆうしゃ）");
}

TEST_CASE("NFKCTextNormalizer - normalize handles full-width numbers", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "１２３４５";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "12345");
}

TEST_CASE("NFKCTextNormalizer - normalize handles full-width ASCII", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ＡＢＣＤＥ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "ABCDE");
}

TEST_CASE("NFKCTextNormalizer - normalize handles complex Japanese text", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ﾄﾞﾗｺﾞﾝｸｴｽﾄX（ﾃﾝ）は、日本のMMORPGです。";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "ドラゴンクエストX(テン)は、日本のMMORPGです。");
}

TEST_CASE("NFKCTextNormalizer - normalize handles NPC speech pattern", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "「どの子を　連れていきますか？」\r\n\r\n\r\n「選んでください」";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "「どの子を　連れていきますか？」\n\n「選んでください」");
}

TEST_CASE("NFKCTextNormalizer - normalize handles quest text", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ｸｴｽﾄ：魔王を倒せ！\r\n\r\n\r\n目標：ﾎﾞｽを倒す";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "クエスト：魔王を倒せ！\n\n目標：ボスを倒す");
}

TEST_CASE("NFKCTextNormalizer - normalize handles empty string", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "");
}

TEST_CASE("NFKCTextNormalizer - normalize handles only newlines", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "\n\n\n\n\n";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "\n\n");
}

TEST_CASE("NFKCTextNormalizer - normalize preserves hiragana", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "あいうえお";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "あいうえお");
}

TEST_CASE("NFKCTextNormalizer - normalize preserves full-width katakana", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "アイウエオ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "アイウエオ");
}

TEST_CASE("NFKCTextNormalizer - normalize handles half-width katakana with dakuten", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ﾄﾞ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "ド");
}

TEST_CASE("NFKCTextNormalizer - normalize handles half-width katakana with handakuten", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "ﾎﾟ";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "ポ");
}

TEST_CASE("NFKCTextNormalizer - normalize handles long Japanese text", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input;
    for (int i = 0; i < 100; ++i)
    {
        input += "「勇者よ、ようこそ！」\n\n\n";
    }
    std::string result = normalizer.normalize(input);
    size_t newline_count = 0;
    for (char c : result)
    {
        if (c == '\n')
            newline_count++;
    }
    REQUIRE(newline_count <= 200);
}

TEST_CASE("NFKCTextNormalizer - normalize handles Japanese punctuation", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "こんにちは。今日は、良い天気ですね！";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "こんにちは。今日は、良い天気ですね！");
}

TEST_CASE("NFKCTextNormalizer - normalize handles mixed content", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "Quest Start! 「冒険の始まりだ！」\r\n\r\n\r\nﾚﾍﾞﾙ：１";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "Quest Start! 「冒険の始まりだ！」\n\nレベル：1");
}

TEST_CASE("NFKCTextNormalizer - normalize handles em dash compatibility", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "―";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "―");
}

TEST_CASE("NFKCTextNormalizer - normalize handles circled numbers", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "①②③";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "123");
}

TEST_CASE("NFKCTextNormalizer - normalize handles parenthesized characters", "[icu_normalizer]")
{
    processing::NFKCTextNormalizer normalizer;
    std::string input = "㈱";
    std::string result = normalizer.normalize(input);
    REQUIRE(result == "(株)");
}

