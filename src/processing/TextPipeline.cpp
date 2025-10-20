#include "TextPipeline.hpp"
#include "LabelProcessor.hpp"
#include "StageRunner.hpp"
#include "TextNormalizer.hpp"
#include "JapaneseTextDetector.hpp"
#include "Diagnostics.hpp"
#include "../utils/Profile.hpp"

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
    PROFILE_SCOPE_CUSTOM("TextPipeline::process");

    const bool verbose = Diagnostics::IsVerbose();
    if (verbose) {
        PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=input raw=" << Diagnostics::Preview(input);
    }

    // Normalizer stage (combine normalize_line_endings + collapse_newlines)
    auto norm_stage = run_stage<std::string>("normalizer", [&]() {
        std::string s = processing::normalize_line_endings(input);
        s = processing::collapse_newlines(s);
        return s;
    });
    if (verbose) {
        if (norm_stage.succeeded) {
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=normalizer status=ok duration="
                      << norm_stage.duration.count() << "us output="
                      << Diagnostics::Preview(norm_stage.result);
        } else {
            PLOG_ERROR_(Diagnostics::kLogInstance) << "[TextPipeline] stage=normalizer status=error duration="
                       << norm_stage.duration.count() << "us reason="
                       << (norm_stage.error ? *norm_stage.error : "unknown");
        }
    }
    if (!norm_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] fallback=original after normalizer failure";
        }
        // On failure, return original input as a safe fallback
        return input;
    }

    auto language_stage = run_stage<bool>("language_filter", [&]() {
        return ContainsJapaneseText(norm_stage.result);
    });
    if (verbose) {
        if (language_stage.succeeded) {
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=language_filter status=ok duration="
                      << language_stage.duration.count() << "us detected="
                      << (language_stage.result ? "jp" : "non-jp");
        } else {
            PLOG_ERROR_(Diagnostics::kLogInstance) << "[TextPipeline] stage=language_filter status=error duration="
                       << language_stage.duration.count() << "us reason="
                       << (language_stage.error ? *language_stage.error : "unknown");
        }
    }
    if (language_stage.succeeded && !language_stage.result) {
        if (verbose) {
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] filtered_out reason=non_japanese";
        }
        return std::string();
    }
    if (!language_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] language_filter failure -> continuing with normalized text";
        }
    }

    // Label processing stage
    auto label_stage = run_stage<std::string>("label_processor", [&]() {
        return impl_->label_processor.processText(norm_stage.result);
    });
    if (verbose) {
        if (label_stage.succeeded) {
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=label_processor status=ok duration="
                      << label_stage.duration.count() << "us input="
                      << Diagnostics::Preview(norm_stage.result) << " output="
                      << Diagnostics::Preview(label_stage.result);
        } else {
            PLOG_ERROR_(Diagnostics::kLogInstance) << "[TextPipeline] stage=label_processor status=error duration="
                       << label_stage.duration.count() << "us input="
                       << Diagnostics::Preview(norm_stage.result) << " reason="
                       << (label_stage.error ? *label_stage.error : "unknown");
        }
    }
    if (!label_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] fallback=normalized after label_processor failure";
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
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=final_collapse status=ok duration="
                      << final_stage.duration.count() << "us input="
                      << Diagnostics::Preview(label_stage.result) << " output="
                      << Diagnostics::Preview(final_stage.result);
        } else {
            PLOG_ERROR_(Diagnostics::kLogInstance) << "[TextPipeline] stage=final_collapse status=error duration="
                       << final_stage.duration.count() << "us input="
                       << Diagnostics::Preview(label_stage.result) << " reason="
                       << (final_stage.error ? *final_stage.error : "unknown");
        }
    }
    if (!final_stage.succeeded) {
        if (verbose) {
            PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] fallback=label_output after final_collapse failure";
        }
        return label_stage.result;
    }

    if (verbose) {
        PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=complete output=" << Diagnostics::Preview(final_stage.result);
    }

    return final_stage.result;
}

} // namespace processing
