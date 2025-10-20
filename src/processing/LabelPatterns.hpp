#pragma once

#include <regex>
#include <string>

namespace label_rules {

// Precompiled regexes for label processing
static const std::regex br_pattern("<br>", std::regex_constants::icase);
static const std::regex select_nc_pattern(R"(<select_nc>([\s\S]*?)<select_end>)",
                                          std::regex_constants::icase | std::regex_constants::ECMAScript);
static const std::regex select_se_off_pattern(R"(<select_se_off>([\s\S]*?)<select_end>)",
                                              std::regex_constants::icase | std::regex_constants::ECMAScript);
static const std::regex speed_pattern(R"(<speed=[^>]*>)");
static const std::regex label_pattern(R"(<[^>]*>)");
static const std::regex attr_pattern(R"(<attr>.*?<end_attr>)");

// Escape a literal string so it can be used in std::regex as a literal match.
// This mirrors std::regex_replace-based escaping but is faster and explicit.
inline std::string escape_regex(const std::string& s)
{
    static const std::string special = R"(\.^$|()[]*+?{}-)";
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s)
    {
        if (special.find(c) != std::string::npos)
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

} // namespace label_rules
