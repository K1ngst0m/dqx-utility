#pragma once

#include <plog/Formatters/TxtFormatter.h>
#include <plog/Util.h>

// UTF-8 aware formatter for file logs.
// This formatter ensures proper UTF-8 encoding for Japanese text on Windows.
struct Utf8FileFormatter
{
    static plog::util::nstring header()
    {
        // No BOM needed - we'll let the OS handle the encoding detection
        return plog::util::nstring();
    }

    static plog::util::nstring format(const plog::Record& record)
    {
        return plog::TxtFormatter::format(record);
    }
};
