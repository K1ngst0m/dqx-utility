#pragma once

#include <string>

namespace processing
{

/// UTF-8 to UTF-32 conversion
std::u32string utf8ToUtf32(const std::string& utf8_str);

/// UTF-32 to UTF-8 conversion
std::string utf32ToUtf8(const std::u32string& utf32_str);

/// Check if a codepoint is in the Katakana Unicode block (U+30A0-U+30FF)
bool isKatakanaChar(char32_t cp);

/// Check if a UTF-32 string contains only Katakana characters
bool isPureKatakana(const std::u32string& s);

/// PUA (Private Use Area) marker constants for entity annotation
/// These markers are used to embed entity metadata in text
constexpr char32_t MARKER_START = U'\uE100';
constexpr char32_t MARKER_SEP = U'\uE101';
constexpr char32_t MARKER_END = U'\uE102';

} // namespace processing
