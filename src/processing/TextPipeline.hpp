#pragma once

#include <memory>
#include <string>
#include "UnknownLabelRepository.hpp"

namespace processing
{

class TextPipeline
{
public:
    explicit TextPipeline(UnknownLabelRepository* repo = nullptr);
    ~TextPipeline();

    [[nodiscard]] std::string process(const std::string& input, const std::string& target_lang = "",
                                      bool use_glossary = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace processing
