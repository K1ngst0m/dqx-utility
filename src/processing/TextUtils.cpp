#include "TextUtils.hpp"
#include <utf8proc.h>

namespace processing
{

std::u32string utf8ToUtf32(const std::string& utf8_str)
{
    std::u32string result;
    if (utf8_str.empty())
        return result;
    
    const utf8proc_uint8_t* str = reinterpret_cast<const utf8proc_uint8_t*>(utf8_str.c_str());
    utf8proc_ssize_t len = static_cast<utf8proc_ssize_t>(utf8_str.size());
    
    utf8proc_ssize_t pos = 0;
    while (pos < len)
    {
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t bytes = utf8proc_iterate(str + pos, len - pos, &codepoint);
        if (bytes <= 0)
            break;
        result.push_back(static_cast<char32_t>(codepoint));
        pos += bytes;
    }
    return result;
}

std::string utf32ToUtf8(const std::u32string& utf32_str)
{
    std::string result;
    for (char32_t cp : utf32_str)
    {
        utf8proc_uint8_t buffer[4];
        utf8proc_ssize_t bytes = utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp), buffer);
        if (bytes > 0)
        {
            result.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(bytes));
        }
    }
    return result;
}

bool isKatakanaChar(char32_t cp)
{
    return (cp >= U'\u30A0' && cp <= U'\u30FF');
}

bool isPureKatakana(const std::u32string& s)
{
    if (s.empty())
        return false;
    for (char32_t cp : s)
    {
        if (!isKatakanaChar(cp))
            return false;
    }
    return true;
}

} // namespace processing
