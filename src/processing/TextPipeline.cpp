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

namespace {

void logInput(const std::string& input)
{
    if (Diagnostics::IsVerbose())
        PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=input raw=" << Diagnostics::Preview(input);
}

void logStageResult(const text_processing::StageResult<std::string>& stage,
                   const char* name, const std::string* input_preview = nullptr)
{
    if (!Diagnostics::IsVerbose())
        return;

    if (stage.succeeded) {
        std::ostringstream oss;
        oss << "[TextPipeline] stage=" << name << " status=ok duration=" << stage.duration.count() << "us";
        if (input_preview)
            oss << " input=" << Diagnostics::Preview(*input_preview);
        oss << " output=" << Diagnostics::Preview(stage.result);
        PLOG_INFO_(Diagnostics::kLogInstance) << oss.str();
    } else {
        std::ostringstream oss;
        oss << "[TextPipeline] stage=" << name << " status=error duration=" << stage.duration.count() << "us";
        if (input_preview)
            oss << " input=" << Diagnostics::Preview(*input_preview);
        oss << " reason=" << (stage.error ? *stage.error : "unknown");
        PLOG_ERROR_(Diagnostics::kLogInstance) << oss.str();
    }
}

void logLanguageDetection(const text_processing::StageResult<bool>& stage)
{
    if (!Diagnostics::IsVerbose())
        return;

    if (stage.succeeded) {
        PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=language_filter status=ok duration="
                  << stage.duration.count() << "us detected=" << (stage.result ? "jp" : "non-jp");
        if (!stage.result)
            PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] filtered_out reason=non_japanese";
    } else {
        PLOG_ERROR_(Diagnostics::kLogInstance) << "[TextPipeline] stage=language_filter status=error duration="
                  << stage.duration.count() << "us reason=" << (stage.error ? *stage.error : "unknown");
        PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] language_filter failure -> continuing with normalized text";
    }
}

std::string logFallback(const char* target, const std::string& fallback_value)
{
    if (Diagnostics::IsVerbose())
        PLOG_WARNING_(Diagnostics::kLogInstance) << "[TextPipeline] fallback=" << target;
    return fallback_value;
}

void logCompletion(const std::string& output)
{
    if (Diagnostics::IsVerbose())
        PLOG_INFO_(Diagnostics::kLogInstance) << "[TextPipeline] stage=complete output=" << Diagnostics::Preview(output);
}

} // anonymous namespace

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

    logInput(input);

    auto norm_stage = run_stage<std::string>("normalizer", [&]() {
        std::string s = processing::normalize_line_endings(input);
        return processing::collapse_newlines(s);
    });
    logStageResult(norm_stage, "normalizer");
    if (!norm_stage.succeeded)
        return logFallback("original", input);

    auto language_stage = run_stage<bool>("language_filter", [&]() {
        return ContainsJapaneseText(norm_stage.result);
    });
    logLanguageDetection(language_stage);
    if (language_stage.succeeded && !language_stage.result)
        return std::string();

    auto label_stage = run_stage<std::string>("label_processor", [&]() {
        return impl_->label_processor.processText(norm_stage.result);
    });
    logStageResult(label_stage, "label_processor", &norm_stage.result);
    if (!label_stage.succeeded)
        return logFallback("normalized", norm_stage.result);

    auto final_stage = run_stage<std::string>("final_collapse", [&]() {
        return processing::collapse_newlines(label_stage.result);
    });
    logStageResult(final_stage, "final_collapse", &label_stage.result);
    if (!final_stage.succeeded)
        return logFallback("label_output", label_stage.result);

    logCompletion(final_stage.result);
    return final_stage.result;
}

} // namespace processing
