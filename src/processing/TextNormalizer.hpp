#pragma once

#include <string>

namespace processing
{

[[nodiscard]] std::string collapse_newlines(const std::string& text);
[[nodiscard]] std::string normalize_line_endings(const std::string& text);

} // namespace processing
