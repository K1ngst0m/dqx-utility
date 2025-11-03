#include "NFKCTextNormalizer.hpp"
#include <utf8proc.h>
#include <plog/Log.h>

namespace processing
{

struct NFKCTextNormalizer::Impl
{
};

NFKCTextNormalizer::NFKCTextNormalizer()
    : impl_(std::make_unique<Impl>())
{
}

NFKCTextNormalizer::~NFKCTextNormalizer() = default;

std::string NFKCTextNormalizer::normalizeLineEndings(const std::string& text) const
{
    if (text.empty())
        return text;

    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                out.push_back('\n');
                ++i;
            }
            else
            {
                out.push_back('\n');
            }
        }
        else
        {
            out.push_back(c);
        }
    }

    return out;
}

std::string NFKCTextNormalizer::collapseNewlines(const std::string& text) const
{
    if (text.empty())
        return text;

    std::string result;
    result.reserve(text.size());

    int consecutive_newlines = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\n')
        {
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

std::string NFKCTextNormalizer::normalize(const std::string& text) const
{
    if (text.empty())
        return text;

    std::string line_normalized = normalizeLineEndings(text);

    utf8proc_uint8_t* normalized = utf8proc_NFKC(reinterpret_cast<const utf8proc_uint8_t*>(line_normalized.c_str()));

    if (!normalized)
    {
        PLOG_WARNING << "NFKC normalization failed, falling back to line ending normalization only";
        return collapseNewlines(line_normalized);
    }

    std::string nfkc_normalized(reinterpret_cast<char*>(normalized));
    free(normalized);

    return collapseNewlines(nfkc_normalized);
}

} // namespace processing

