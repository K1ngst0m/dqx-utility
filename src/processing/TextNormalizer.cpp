#include "TextNormalizer.hpp"
#include <string>
#include <sstream>

namespace processing
{

std::string normalize_line_endings(const std::string& text)
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
                // skip the '\r', '\n' pair and emit single '\n'
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

std::string collapse_newlines(const std::string& text)
{
    if (text.empty())
        return text;

    std::string normalized = normalize_line_endings(text);
    std::string result;
    result.reserve(normalized.size());

    int consecutive_newlines = 0;
    for (size_t i = 0; i < normalized.size(); ++i)
    {
        char c = normalized[i];
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

} // namespace processing
