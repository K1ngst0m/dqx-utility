#pragma once

#include <string_view>

namespace processing {

[[nodiscard]] bool ContainsJapaneseText(std::string_view text);

} // namespace processing
