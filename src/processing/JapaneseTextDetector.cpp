#include "processing/JapaneseTextDetector.hpp"

#include <cstddef>
#include <cstdint>

namespace processing {

namespace {

bool isHiragana(uint32_t cp)
{
    return cp >= 0x3040u && cp <= 0x309Fu;
}

bool isKatakana(uint32_t cp)
{
    return (cp >= 0x30A0u && cp <= 0x30FFu) || (cp >= 0x31F0u && cp <= 0x31FFu);
}

bool isHalfwidthKatakana(uint32_t cp)
{
    return cp >= 0xFF66u && cp <= 0xFF9Fu;
}

bool isCjkUnified(uint32_t cp)
{
    return (cp >= 0x4E00u && cp <= 0x9FFFu) ||
           (cp >= 0x3400u && cp <= 0x4DBFu) ||
           (cp >= 0xF900u && cp <= 0xFAFFu);
}

bool isJapaneseSpecificPunctuation(uint32_t cp)
{
    switch (cp)
    {
    case 0x3005u: // 々
    case 0x3006u: // 〆
    case 0x300Cu: // 「
    case 0x300Du: // 」
    case 0x300Eu: // 『
    case 0x300Fu: // 』
    case 0x301Cu: // 〜
    case 0x301Du: // 〝
    case 0x301Eu: // 〞
    case 0x301Fu: // 〟
    case 0x303Bu: // 〻
    case 0x30FBu: // ・
    case 0x30FCu: // ー
    case 0xFF70u: // ｰ (halfwidth long sound)
        return true;
    default:
        return false;
    }
}

bool decodeNextUtf8(std::string_view text, size_t& index, uint32_t& codepoint)
{
    const unsigned char lead = static_cast<unsigned char>(text[index]);

    if (lead < 0x80u)
    {
        codepoint = lead;
        ++index;
        return true;
    }

    size_t remaining = text.size() - index;
    if (lead < 0xC2u)
    {
        ++index;
        return false;
    }

    if (lead < 0xE0u)
    {
        if (remaining < 2) { index = text.size(); return false; }
        unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
        if ((c1 & 0xC0u) != 0x80u) { index += 1; return false; }
        codepoint = ((lead & 0x1Fu) << 6) | (c1 & 0x3Fu);
        index += 2;
        return true;
    }

    if (lead < 0xF0u)
    {
        if (remaining < 3) { index = text.size(); return false; }
        unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
        unsigned char c2 = static_cast<unsigned char>(text[index + 2]);
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u)
        {
            index += 1;
            return false;
        }
        if (lead == 0xE0u && c1 < 0xA0u)
        {
            index += 1;
            return false;
        }
        if (lead == 0xEDu && c1 >= 0xA0u)
        {
            index += 1;
            return false;
        }
        codepoint = ((lead & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
        index += 3;
        return true;
    }

    if (lead < 0xF5u)
    {
        if (remaining < 4) { index = text.size(); return false; }
        unsigned char c1 = static_cast<unsigned char>(text[index + 1]);
        unsigned char c2 = static_cast<unsigned char>(text[index + 2]);
        unsigned char c3 = static_cast<unsigned char>(text[index + 3]);
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u)
        {
            index += 1;
            return false;
        }
        if (lead == 0xF0u && c1 < 0x90u)
        {
            index += 1;
            return false;
        }
        if (lead == 0xF4u && c1 >= 0x90u)
        {
            index += 1;
            return false;
        }
        codepoint = ((lead & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
        index += 4;
        return true;
    }

    ++index;
    return false;
}

} // namespace

bool ContainsJapaneseText(std::string_view text)
{
    bool has_cjk = false;
    bool has_japanese_punct = false;

    size_t index = 0;
    while (index < text.size())
    {
        uint32_t codepoint = 0;
        if (!decodeNextUtf8(text, index, codepoint))
        {
            continue;
        }

        if (codepoint == 0xFEFFu || codepoint == 0xFFFEu || codepoint == 0xFFFFu)
        {
            continue;
        }

        if (codepoint == 0xFFFDu)
        {
            continue;
        }

        if (isHiragana(codepoint) || isKatakana(codepoint) || isHalfwidthKatakana(codepoint))
        {
            return true;
        }

        if (isCjkUnified(codepoint))
        {
            has_cjk = true;
        }

        if (isJapaneseSpecificPunctuation(codepoint))
        {
            has_japanese_punct = true;
        }
    }

    return has_cjk && has_japanese_punct;
}

} // namespace processing
