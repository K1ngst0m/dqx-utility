#include "TextPipeline.hpp"
#include "../translate/LabelProcessor.hpp"
#include "StageRunner.hpp"
#include "../processing/TextNormalizer.hpp"
#include "Diagnostics.hpp"

#include <memory>
#include <plog/Log.h>

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
    const bool verbose = Diagnostics::IsVerbose();
    if (verbose) {
        PLOG_INFO << "[TextPipeline] stage=input raw=" << Diagnostics::Preview(input);
    }

    // Normalizer stage (combine normalize_line_endings + collapse_newlines)
    auto norm_stage = run_stage<std::string>("normalizer", [&]() {
        std::string s = processing::normalize_line_endings(input);
        s = processing::collapse_newlines(s);
        return s;
    });
    if (verbose) {
        if (norm_stage.succeeded) {
            PLOG_INFO << "[TextPipeline] stage=normalizer status=ok duration="
                      << norm_stage.duration.count() << "us output="
                      << Diagnostics::Preview(norm_stage.result);
        } else {
            PLOG_ERROR << "[TextPipeline] stage=normalizer status=error duration="
                       << norm_stage.duration.count() << "us reason="
                       << (norm_stage.error ? *norm_stage.error : "unknown");
        }
    }
    if (!norm_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING << "[TextPipeline] fallback=original after normalizer failure";
        }
        // On failure, return original input as a safe fallback
        return input;
    }

    // Label processing stage
    auto label_stage = run_stage<std::string>("label_processor", [&]() {
        return impl_->label_processor.processText(norm_stage.result);
    });
    if (verbose) {
        if (label_stage.succeeded) {
            PLOG_INFO << "[TextPipeline] stage=label_processor status=ok duration="
                      << label_stage.duration.count() << "us input="
                      << Diagnostics::Preview(norm_stage.result) << " output="
                      << Diagnostics::Preview(label_stage.result);
        } else {
            PLOG_ERROR << "[TextPipeline] stage=label_processor status=error duration="
                       << label_stage.duration.count() << "us input="
                       << Diagnostics::Preview(norm_stage.result) << " reason="
                       << (label_stage.error ? *label_stage.error : "unknown");
        }
    }
    if (!label_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING << "[TextPipeline] fallback=normalized after label_processor failure";
        }
        // Fallback to normalized text if label processing failed
        return norm_stage.result;
    }

    // Final collapse stage to ensure spacing is normalized after label processing
    auto final_stage = run_stage<std::string>("final_collapse", [&]() {
        return processing::collapse_newlines(label_stage.result);
    });
    if (verbose) {
        if (final_stage.succeeded) {
            PLOG_INFO << "[TextPipeline] stage=final_collapse status=ok duration="
                      << final_stage.duration.count() << "us input="
                      << Diagnostics::Preview(label_stage.result) << " output="
                      << Diagnostics::Preview(final_stage.result);
        } else {
            PLOG_ERROR << "[TextPipeline] stage=final_collapse status=error duration="
                       << final_stage.duration.count() << "us input="
                       << Diagnostics::Preview(label_stage.result) << " reason="
                       << (final_stage.error ? *final_stage.error : "unknown");
        }
    }
    if (!final_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING << "[TextPipeline] fallback=label_output after final_collapse failure";
        }
        return label_stage.result;
    }

    if (verbose) {
        PLOG_INFO << "[TextPipeline] stage=complete output=" << Diagnostics::Preview(final_stage.result);
    }

    return final_stage.result;
}

} // namespace processing
