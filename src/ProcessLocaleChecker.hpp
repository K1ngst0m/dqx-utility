#pragma once

#include <string>

enum class ProcessLocale
{
    Unknown,
    Japanese,
    NonJapanese
};

class ProcessLocaleChecker
{
public:
    // Check the locale by examining the window title
    // If the window title matches "ドラゴンクエストＸ　オンライン", it's Japanese locale
    static ProcessLocale checkProcessLocale(const std::string& processName);
};
