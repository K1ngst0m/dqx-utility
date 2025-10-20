#pragma once

#include "TextProcessingTypes.hpp"
#include "Diagnostics.hpp"
#include <chrono>
#include <string>
#include <plog/Log.h>
#include <utility>
#include <exception>

#include "../utils/Profile.hpp"
#include "../utils/ErrorReporter.hpp"

namespace processing {

// Utility to run a stage (callable returning T) and produce text_processing::StageResult<T>
// Measures duration and logs errors. Keeps stages consistent for pipeline tracing.
template<typename T, typename Fn>
text_processing::StageResult<T> run_stage(const std::string& stage_name, Fn&& fn)
{
    PROFILE_SCOPE_CUSTOM(stage_name);

    using namespace std::chrono;
    auto start = high_resolution_clock::now();
    try
    {
        T res = fn();
        auto end = high_resolution_clock::now();
        auto dur = duration_cast<std::chrono::microseconds>(end - start);
        auto sr = text_processing::StageResult<T>::success(std::move(res), dur, stage_name);
        if (Diagnostics::IsVerbose()) {
            PLOG_INFO_(Diagnostics::kLogInstance) << "Stage '" << stage_name << "' succeeded in " << dur.count() << "us";
        }
        return sr;
    }
    catch (const std::exception& ex)
    {
        auto end = high_resolution_clock::now();
        auto dur = duration_cast<std::chrono::microseconds>(end - start);
        PLOG_ERROR_(Diagnostics::kLogInstance) << "Stage '" << stage_name << "' failed in " << dur.count() << "us: " << ex.what();
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
            "Text pipeline stage failed",
            stage_name + ": " + ex.what());
        return text_processing::StageResult<T>::failure(ex.what(), dur, stage_name);
    }
    catch (...)
    {
        auto end = high_resolution_clock::now();
        auto dur = duration_cast<std::chrono::microseconds>(end - start);
        PLOG_ERROR_(Diagnostics::kLogInstance) << "Stage '" << stage_name << "' failed with unknown exception in " << dur.count() << "us";
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
            "Text pipeline stage failed",
            stage_name + ": unknown exception");
        return text_processing::StageResult<T>::failure("unknown exception", dur, stage_name);
    }
}

} // namespace processing
