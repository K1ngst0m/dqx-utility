#include "TextPipeline.hpp"
#include "../translate/LabelProcessor.hpp"
#include "StageRunner.hpp"
#include "../processing/TextNormalizer.hpp"

#include <memory>

namespace processing {

struct TextPipeline::Impl {
    explicit Impl(UnknownLabelRepository* repo)
        : label_processor(repo)
    {
    }

    LabelProcessor label_processor;
};

TextPipeline::TextPipeline(UnknownLabelRepository* repo)
    : impl_(std::make_unique<Impl>(repo))
{
}

TextPipeline::~TextPipeline() = default;

std::string TextPipeline::process(const std::string& input)
{
    // Normalizer stage (combine normalize_line_endings + collapse_newlines)
    auto norm_stage = run_stage<std::string>("normalizer", [&]() {
        std::string s = processing::normalize_line_endings(input);
        s = processing::collapse_newlines(s);
        return s;
    });
    if (!norm_stage.succeeded) {
        // On failure, return original input as a safe fallback
        return input;
    }

    // Label processing stage
    auto label_stage = run_stage<std::string>("label_processor", [&]() {
        return impl_->label_processor.processText(norm_stage.result);
    });
    if (!label_stage.succeeded) {
        // Fallback to normalized text if label processing failed
        return norm_stage.result;
    }

    // Final collapse stage to ensure spacing is normalized after label processing
    auto final_stage = run_stage<std::string>("final_collapse", [&]() {
        return processing::collapse_newlines(label_stage.result);
    });
    if (!final_stage.succeeded) {
        return label_stage.result;
    }

    return final_stage.result;
}

} // namespace processing
