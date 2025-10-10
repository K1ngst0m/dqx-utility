#pragma once

#include <string>

namespace processing {

// Collapse multiple consecutive newlines (CR/LF normalized) into at most two consecutive '\n'.
// This preserves paragraph breaks while preventing excessive spacing.
std::string collapse_newlines(const std::string& text);

// Normalize line endings: convert "\r\n" and lone "\r" into '\n'.
std::string normalize_line_endings(const std::string& text);

} // namespace processing
