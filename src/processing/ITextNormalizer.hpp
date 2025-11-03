#pragma once

#include <string>

namespace processing
{

class ITextNormalizer
{
public:
    virtual ~ITextNormalizer() = default;

    // Converts \r\n and \r to \n
    [[nodiscard]] virtual std::string normalizeLineEndings(const std::string& text) const = 0;

    // Limits consecutive newlines to maximum 2
    [[nodiscard]] virtual std::string collapseNewlines(const std::string& text) const = 0;

    // Full normalization pipeline: line endings + Unicode NFKC + collapse
    [[nodiscard]] virtual std::string normalize(const std::string& text) const = 0;
};

} // namespace processing

