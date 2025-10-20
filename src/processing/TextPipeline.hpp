#pragma once

#include <memory>
#include <string>
#include "UnknownLabelRepository.hpp"

namespace processing {

class TextPipeline {
public:
    explicit TextPipeline(UnknownLabelRepository* repo = nullptr);
    ~TextPipeline();

    // Process raw dialog text and return text ready for translation submission
    std::string process(const std::string& input);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace processing
