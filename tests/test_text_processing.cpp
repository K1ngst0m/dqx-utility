#include <catch2/catch_test_macros.hpp>
#include <string>
#include <cstring>
#include <algorithm>

#include "processing/JapaneseTextDetector.hpp"
#include "processing/TextPipeline.hpp"

namespace
{
void safe_copy_utf8(char* dest, size_t dest_size, const std::string& src)
{
    if (dest_size == 0)
        return;
    if (src.empty())
    {
        dest[0] = '\0';
        return;
    }

    size_t copy_len = std::min(src.length(), dest_size - 1);

    if (copy_len < src.length())
    {
        while (copy_len > 0 && (src[copy_len] & 0x80) && !(src[copy_len] & 0x40))
        {
            --copy_len;
        }
    }

    std::memcpy(dest, src.c_str(), copy_len);
    dest[copy_len] = '\0';
}

std::string collapse_newlines(const std::string& text)
{
    if (text.empty())
        return text;

    std::string result;
    result.reserve(text.size());

    int consecutive_newlines = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\n' || c == '\r')
        {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            {
                continue;
            }

            consecutive_newlines++;
            if (consecutive_newlines <= 2)
            {
                result += '\n';
            }
        }
        else
        {
            consecutive_newlines = 0;
            result += c;
        }
    }

    return result;
}
} // namespace

TEST_CASE("safe_copy_utf8 handles basic ASCII", "[text_processing]")
{
    char buffer[256];
    safe_copy_utf8(buffer, sizeof(buffer), "Hello World");
    REQUIRE(std::string(buffer) == "Hello World");
}

TEST_CASE("safe_copy_utf8 handles empty string", "[text_processing]")
{
    char buffer[256];
    buffer[0] = 'X';
    safe_copy_utf8(buffer, sizeof(buffer), "");
    REQUIRE(buffer[0] == '\0');
}

TEST_CASE("safe_copy_utf8 handles zero buffer size", "[text_processing]")
{
    char buffer[1] = { 'X' };
    safe_copy_utf8(buffer, 0, "test");
    REQUIRE(buffer[0] == 'X');
}

TEST_CASE("safe_copy_utf8 truncates at UTF-8 boundary", "[text_processing]")
{
    char buffer[10];
    std::string japanese = "こんにちは";
    safe_copy_utf8(buffer, sizeof(buffer), japanese);

    size_t len = std::strlen(buffer);
    REQUIRE(len < sizeof(buffer));
    REQUIRE(len > 0);

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        if ((c & 0x80) != 0)
        {
            if ((c & 0xE0) == 0xC0)
            {
                REQUIRE(i + 1 < len);
            }
            else if ((c & 0xF0) == 0xE0)
            {
                REQUIRE(i + 2 < len);
            }
            else if ((c & 0xF8) == 0xF0)
            {
                REQUIRE(i + 3 < len);
            }
        }
    }
}

TEST_CASE("safe_copy_utf8 handles multi-byte UTF-8", "[text_processing]")
{
    char buffer[256];
    std::string mixed = "Hello 世界 Test テスト";
    safe_copy_utf8(buffer, sizeof(buffer), mixed);
    REQUIRE(std::string(buffer) == mixed);
}

TEST_CASE("safe_copy_utf8 preserves Japanese characters", "[text_processing]")
{
    char buffer[256];
    std::string text = "勇者よ、よく来てくれた！";
    safe_copy_utf8(buffer, sizeof(buffer), text);
    REQUIRE(std::string(buffer) == text);
}

TEST_CASE("collapse_newlines handles single newline", "[text_processing]")
{
    std::string input = "Line 1\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\nLine 2");
}

TEST_CASE("collapse_newlines allows up to 2 consecutive newlines", "[text_processing]")
{
    std::string input = "Line 1\n\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines collapses 3+ consecutive newlines to 2", "[text_processing]")
{
    std::string input = "Line 1\n\n\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines collapses many consecutive newlines", "[text_processing]")
{
    std::string input = "Line 1\n\n\n\n\n\n\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines handles Windows line endings", "[text_processing]")
{
    std::string input = "Line 1\r\n\r\n\r\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines handles mixed line endings", "[text_processing]")
{
    std::string input = "Line 1\r\n\n\r\nLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines handles empty string", "[text_processing]")
{
    std::string input = "";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "");
}

TEST_CASE("collapse_newlines handles string with only newlines", "[text_processing]")
{
    std::string input = "\n\n\n\n\n";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "\n\n");
}

TEST_CASE("collapse_newlines preserves text with no newlines", "[text_processing]")
{
    std::string input = "Single line text with no breaks";
    std::string result = collapse_newlines(input);
    REQUIRE(result == input);
}

TEST_CASE("collapse_newlines handles newlines at start", "[text_processing]")
{
    std::string input = "\n\n\nText starts here";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "\n\nText starts here");
}

TEST_CASE("collapse_newlines handles newlines at end", "[text_processing]")
{
    std::string input = "Text ends here\n\n\n";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Text ends here\n\n");
}

TEST_CASE("collapse_newlines handles multiple sections with excess newlines", "[text_processing]")
{
    std::string input = "Section 1\n\n\n\nSection 2\n\n\n\n\nSection 3";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Section 1\n\nSection 2\n\nSection 3");
}

TEST_CASE("collapse_newlines with real dialog patterns", "[text_processing]")
{
    std::string input = "「ようこそ！」\n\n\n\n「冒険者よ、この村へようこそ！」";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "「ようこそ！」\n\n「冒険者よ、この村へようこそ！」");
}

TEST_CASE("collapse_newlines preserves single empty line between paragraphs", "[text_processing]")
{
    std::string input = "Paragraph one.\n\nParagraph two.";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Paragraph one.\n\nParagraph two.");
}

TEST_CASE("collapse_newlines with Japanese text and multiple newlines", "[text_processing]")
{
    std::string input = "最初の行\n\n\n\n\n二番目の行\n\n\n\n三番目の行";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "最初の行\n\n二番目の行\n\n三番目の行");
}

TEST_CASE("safe_copy_utf8 with collapsed newlines integration", "[text_processing]")
{
    char buffer[256];
    std::string input = "Text with\n\n\n\ntoo many lines";
    std::string collapsed = collapse_newlines(input);
    safe_copy_utf8(buffer, sizeof(buffer), collapsed);
    REQUIRE(std::string(buffer) == "Text with\n\ntoo many lines");
}

TEST_CASE("collapse_newlines handles dialog with NPC speech patterns", "[text_processing]")
{
    std::string input = "NPC: Hello!\n\n\n\n\nNPC: How are you?\n\n\n\nPlayer: I'm fine.";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "NPC: Hello!\n\nNPC: How are you?\n\nPlayer: I'm fine.");
}

TEST_CASE("collapse_newlines handles choice menu format", "[text_processing]")
{
    std::string input = "Choose:\n• Option 1\n• Option 2\n\n\n\n• Option 3";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Choose:\n• Option 1\n• Option 2\n\n• Option 3");
}

TEST_CASE("collapse_newlines edge case: alternating text and newlines", "[text_processing]")
{
    std::string input = "A\n\n\nB\n\n\nC\n\n\nD";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "A\n\nB\n\nC\n\nD");
}

TEST_CASE("safe_copy_utf8 buffer boundary with emoji", "[text_processing]")
{
    char buffer[20];
    std::string emoji_text = "Test 😀 Emoji";
    safe_copy_utf8(buffer, sizeof(buffer), emoji_text);

    size_t len = std::strlen(buffer);
    REQUIRE(len < sizeof(buffer));
}

TEST_CASE("collapse_newlines with carriage return only", "[text_processing]")
{
    std::string input = "Line 1\r\r\rLine 2";
    std::string result = collapse_newlines(input);
    REQUIRE(result == "Line 1\n\nLine 2");
}

TEST_CASE("collapse_newlines performance with large text", "[text_processing]")
{
    std::string input;
    for (int i = 0; i < 100; ++i)
    {
        input += "Line " + std::to_string(i) + "\n\n\n\n";
    }

    std::string result = collapse_newlines(input);

    size_t newline_count = std::count(result.begin(), result.end(), '\n');
    REQUIRE(newline_count <= 200);
}

TEST_CASE("safe_copy_utf8 exact buffer size boundary", "[text_processing]")
{
    char buffer[6];
    safe_copy_utf8(buffer, sizeof(buffer), "Hello");
    REQUIRE(std::string(buffer) == "Hello");
}

TEST_CASE("safe_copy_utf8 one byte over boundary", "[text_processing]")
{
    char buffer[6];
    safe_copy_utf8(buffer, sizeof(buffer), "Hello!");
    REQUIRE(std::strlen(buffer) == 5);
}

TEST_CASE("ContainsJapaneseText detects hiragana and katakana from dialog log", "[japanese_detection]")
{
    const std::string from_log = "「どの子を　連れていきますか？\n";
    REQUIRE(processing::ContainsJapaneseText(from_log));
}

TEST_CASE("ContainsJapaneseText ignores plain ASCII", "[japanese_detection]")
{
    const std::string ascii = "This is an English line with numbers 12345.";
    REQUIRE_FALSE(processing::ContainsJapaneseText(ascii));
}

TEST_CASE("ContainsJapaneseText ignores replacement characters", "[japanese_detection]")
{
    const std::string corrupted = "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD";
    REQUIRE_FALSE(processing::ContainsJapaneseText(corrupted));
}

TEST_CASE("ContainsJapaneseText remains false for Chinese text", "[japanese_detection]")
{
    const std::string chinese = "这是中文，测试";
    REQUIRE_FALSE(processing::ContainsJapaneseText(chinese));
}

TEST_CASE("ContainsJapaneseText detects Kanji when paired with Japanese punctuation", "[japanese_detection]")
{
    const std::string kanji_with_quotes = "「勇者」";
    REQUIRE(processing::ContainsJapaneseText(kanji_with_quotes));
}

TEST_CASE("ContainsJapaneseText detects halfwidth katakana", "[japanese_detection]")
{
    const std::string halfwidth = "ｶﾀｶﾅ";
    REQUIRE(processing::ContainsJapaneseText(halfwidth));
}

TEST_CASE("ContainsJapaneseText handles mixed language lines", "[japanese_detection]")
{
    const std::string mixed = "Quest Start! 「冒険の始まりだ！」";
    REQUIRE(processing::ContainsJapaneseText(mixed));
}

TEST_CASE("TextPipeline filters out non-Japanese text", "[text_pipeline]")
{
    processing::TextPipeline pipeline;
    auto result = pipeline.process("This line should be ignored.");
    REQUIRE(result.empty());
}

TEST_CASE("TextPipeline keeps Japanese text", "[text_pipeline]")
{
    processing::TextPipeline pipeline;
    auto result = pipeline.process("「旅人よ、ようこそ！」");
    REQUIRE_FALSE(result.empty());
}
