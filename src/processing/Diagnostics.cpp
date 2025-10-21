#include "Diagnostics.hpp"
#include "../utils/ErrorReporter.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>

#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>

namespace processing
{

std::atomic<bool> Diagnostics::verbose_{ false };
std::atomic<std::size_t> Diagnostics::max_preview_{ 160 };

namespace
{
std::once_flag g_logger_once;
}

void Diagnostics::InitializeLogger()
{
    std::call_once(
        g_logger_once,
        []
        {
            std::error_code ec;
            std::filesystem::create_directories("logs", ec);
            if (ec)
            {
                utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization,
                                                    "Failed to create diagnostics log directory", ec.message());
            }

            try
            {
                static plog::RollingFileAppender<plog::TxtFormatter> dialog_appender("logs/dialog.log",
                                                                                     1024 * 1024 * 10, 3);
                plog::init<kLogInstance>(plog::info, &dialog_appender);
            }
            catch (const std::exception& ex)
            {
                utils::ErrorReporter::ReportError(utils::ErrorCategory::Initialization,
                                                  "Failed to initialize diagnostics logger", ex.what());
            }
            catch (...)
            {
                utils::ErrorReporter::ReportError(utils::ErrorCategory::Initialization,
                                                  "Failed to initialize diagnostics logger", "Unknown exception");
            }
        });
}

void Diagnostics::SetVerbose(bool enabled) noexcept
{
    InitializeLogger();
    verbose_.store(enabled, std::memory_order_relaxed);
}

bool Diagnostics::IsVerbose() noexcept { return verbose_.load(std::memory_order_relaxed); }

void Diagnostics::SetMaxPreview(std::size_t bytes) noexcept
{
    if (bytes == 0)
        bytes = 1;
    max_preview_.store(bytes, std::memory_order_relaxed);
}

std::size_t Diagnostics::MaxPreview() noexcept { return max_preview_.load(std::memory_order_relaxed); }

std::string Diagnostics::Preview(std::string_view text)
{
    InitializeLogger();
    const std::size_t limit = MaxPreview();
    std::string out;
    out.reserve(std::min(text.size(), limit) + 16);

    std::size_t count = 0;
    for (char ch : text)
    {
        if (count >= limit)
        {
            break;
        }
        switch (ch)
        {
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
        ++count;
    }

    if (text.size() > limit)
    {
        out += "... (";
        out += std::to_string(text.size());
        out += " bytes)";
    }

    sanitize(out);
    return out;
}

void Diagnostics::sanitize(std::string& text)
{
    auto is_control = [](unsigned char c)
    {
        return c < 0x20 && c != '\n' && c != '\r' && c != '\t';
    };
    std::replace_if(text.begin(), text.end(), is_control, '?');
}

} // namespace processing
