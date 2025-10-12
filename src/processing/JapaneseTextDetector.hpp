#pragma once

#include <string_view>

namespace processing {

// Returns true if the provided UTF-8 text contains indicative Japanese characters.
[[nodiscard]] bool ContainsJapaneseText(std::string_view text);

} // namespace processing
