#include "ErrorReporter.hpp"
#include <plog/Log.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace utils {

// Static member initialization
std::mutex ErrorReporter::s_mutex;
std::vector<ErrorReport> ErrorReporter::s_error_queue;

// ErrorReport constructor
ErrorReport::ErrorReport(ErrorCategory cat, ErrorSeverity sev, std::string user_msg, std::string tech_details)
    : category(cat)
    , severity(sev)
    , user_message(std::move(user_msg))
    , technical_details(std::move(tech_details))
    , timestamp(ErrorReporter::GetTimestamp())
    , is_fatal(sev == ErrorSeverity::Fatal)
{
}

void ErrorReporter::ReportError(ErrorCategory category, ErrorSeverity severity,
                                const std::string& user_message,
                                const std::string& technical_details)
{
    // Create error report
    ErrorReport report(category, severity, user_message, technical_details);

    // Log to plog
    std::string log_msg = "[" + CategoryToString(category) + "] " + user_message;
    if (!technical_details.empty())
    {
        log_msg += " | Details: " + technical_details;
    }

    switch (severity)
    {
    case ErrorSeverity::Info:
        PLOG_INFO << log_msg;
        break;
    case ErrorSeverity::Warning:
        PLOG_WARNING << log_msg;
        break;
    case ErrorSeverity::Error:
        PLOG_ERROR << log_msg;
        break;
    case ErrorSeverity::Fatal:
        PLOG_FATAL << log_msg;
        break;
    }

    // Add to queue for UI display
    std::lock_guard<std::mutex> lock(s_mutex);
    s_error_queue.push_back(std::move(report));

    // Limit queue size to prevent memory growth
    if (s_error_queue.size() > MAX_QUEUE_SIZE)
    {
        s_error_queue.erase(s_error_queue.begin());
    }
}

void ErrorReporter::ReportFatal(ErrorCategory category,
                               const std::string& user_message,
                               const std::string& technical_details)
{
    ReportError(category, ErrorSeverity::Fatal, user_message, technical_details);
}

void ErrorReporter::ReportError(ErrorCategory category,
                               const std::string& user_message,
                               const std::string& technical_details)
{
    ReportError(category, ErrorSeverity::Error, user_message, technical_details);
}

void ErrorReporter::ReportWarning(ErrorCategory category,
                                 const std::string& user_message,
                                 const std::string& technical_details)
{
    ReportError(category, ErrorSeverity::Warning, user_message, technical_details);
}

bool ErrorReporter::HasPendingErrors()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return !s_error_queue.empty();
}

std::vector<ErrorReport> ErrorReporter::GetPendingErrors()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    std::vector<ErrorReport> errors = s_error_queue;
    s_error_queue.clear();
    return errors;
}

ErrorReport ErrorReporter::GetLastError()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_error_queue.empty())
    {
        return ErrorReport();
    }
    return s_error_queue.back();
}

void ErrorReporter::ClearErrors()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_error_queue.clear();
}

std::string ErrorReporter::CategoryToString(ErrorCategory category)
{
    switch (category)
    {
    case ErrorCategory::Initialization:   return "Initialization";
    case ErrorCategory::MemoryHook:       return "Memory/Hook";
    case ErrorCategory::ProcessDetection: return "Process Detection";
    case ErrorCategory::Configuration:    return "Configuration";
    case ErrorCategory::IPC:              return "IPC";
    case ErrorCategory::Translation:      return "Translation";
    case ErrorCategory::Unknown:          return "Unknown";
    default:                              return "Unknown";
    }
}

std::string ErrorReporter::SeverityToString(ErrorSeverity severity)
{
    switch (severity)
    {
    case ErrorSeverity::Info:    return "Info";
    case ErrorSeverity::Warning: return "Warning";
    case ErrorSeverity::Error:   return "Error";
    case ErrorSeverity::Fatal:   return "Fatal";
    default:                     return "Unknown";
    }
}

std::string ErrorReporter::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace utils
