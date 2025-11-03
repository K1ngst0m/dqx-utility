#pragma once

#include "ITextNormalizer.hpp"
#include <memory>

namespace processing
{

class NFKCTextNormalizer : public ITextNormalizer
{
public:
    NFKCTextNormalizer();
    ~NFKCTextNormalizer() override;

    NFKCTextNormalizer(const NFKCTextNormalizer&) = delete;
    NFKCTextNormalizer& operator=(const NFKCTextNormalizer&) = delete;

    [[nodiscard]] std::string normalizeLineEndings(const std::string& text) const override;
    [[nodiscard]] std::string collapseNewlines(const std::string& text) const override;
    [[nodiscard]] std::string normalize(const std::string& text) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace processing

