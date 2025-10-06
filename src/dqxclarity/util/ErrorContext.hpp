#pragma once

#include <string>
#include <functional>

namespace dqxclarity {

/**
 * @brief Simple error context for dqxclarity standalone library
 * 
 * This is a minimal error reporting system that does NOT depend on external
 * libraries (plog, ImGui, etc). It uses callbacks to allow the parent application
 * to handle errors in its own way.
 * 
 * Design principle: dqxclarity remains standalone and does not depend on main project
 */

enum class ErrorSeverityLevel
{
    Info,
    Warning,
    Error
};

struct ErrorInfo
{
    ErrorSeverityLevel level;
    std::string message;
    std::string details;

    ErrorInfo() = default;
    ErrorInfo(ErrorSeverityLevel lvl, std::string msg, std::string det = "")
        : level(lvl), message(std::move(msg)), details(std::move(det))
    {
    }
};

/**
 * @brief Callback type for error reporting
 * 
 * The parent application should set this callback to receive error notifications
 * from dqxclarity and handle them appropriately (log, show UI, etc)
 */
using ErrorCallback = std::function<void(const ErrorInfo&)>;

/**
 * @brief Error context for dqxclarity operations
 * 
 * Usage in dqxclarity code:
 *   ErrorContext ctx;
 *   ctx.SetCallback([](const ErrorInfo& err) {
 *       // Handle error in parent app
 *   });
 *   ctx.ReportError("Pattern not found", "DialogHook pattern scan failed");
 * 
 * Usage in parent application:
 *   ctx.SetCallback([](const ErrorInfo& err) {
 *       ErrorReporter::ReportError(category, severity, err.message, err.details);
 *   });
 */
class ErrorContext
{
public:
    ErrorContext() = default;
    ~ErrorContext() = default;

    /**
     * @brief Set error callback for handling errors
     */
    void SetCallback(ErrorCallback callback)
    {
        callback_ = std::move(callback);
    }

    /**
     * @brief Report an error
     */
    void ReportError(const std::string& message, const std::string& details = "")
    {
        Report(ErrorSeverityLevel::Error, message, details);
    }

    /**
     * @brief Report a warning
     */
    void ReportWarning(const std::string& message, const std::string& details = "")
    {
        Report(ErrorSeverityLevel::Warning, message, details);
    }

    /**
     * @brief Report informational message
     */
    void ReportInfo(const std::string& message, const std::string& details = "")
    {
        Report(ErrorSeverityLevel::Info, message, details);
    }

    /**
     * @brief Check if callback is set
     */
    bool HasCallback() const
    {
        return static_cast<bool>(callback_);
    }

private:
    void Report(ErrorSeverityLevel level, const std::string& message, const std::string& details)
    {
        if (callback_)
        {
            ErrorInfo info(level, message, details);
            callback_(info);
        }
    }

    ErrorCallback callback_;
};

} // namespace dqxclarity
