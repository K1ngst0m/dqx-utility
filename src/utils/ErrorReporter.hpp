#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace utils {

enum class ErrorCategory
{
    Initialization,   // SDL, ImGui, window creation
    MemoryHook,       // DialogHook, memory operations
    ProcessDetection, // DQXGame.exe not found
    Configuration,    // TOML parsing, invalid config
    IPC,              // TextSourceClient connection
    Translation,      // API failures (OpenAI, Google)
    Unknown
};

enum class ErrorSeverity
{
    Info,    // Informational, no action needed
    Warning, // Degraded functionality, but continues
    Error,   // Operation failed, but app can continue
    Fatal    // Critical error, app should exit
};

struct ErrorReport
{
    ErrorCategory category;
    ErrorSeverity severity;
    std::string user_message;      // Non-technical, actionable message for users
    std::string technical_details; // Technical details for logs/bug reports
    std::string timestamp;
    bool is_fatal;

    ErrorReport() = default;
    ErrorReport(ErrorCategory cat, ErrorSeverity sev, std::string user_msg, std::string tech_details);
};

/**
 * @brief Thread-safe error reporter for main application
 * 
 * Collects errors from various subsystems and queues them for UI display.
 * Uses plog for logging, maintains error queue for UI consumption.
 * 
 * Usage:
 *   ErrorReporter::ReportError(ErrorCategory::Initialization, ErrorSeverity::Fatal,
 *                              "Failed to initialize graphics",
 *                              "SDL_CreateWindow returned NULL");
 *   
 *   // In main loop:
 *   if (ErrorReporter::HasPendingErrors()) {
 *       auto errors = ErrorReporter::GetPendingErrors();
 *       // Show in UI...
 *   }
 */
class ErrorReporter
{
public:
    /**
     * @brief Report an error to the system
     * @param category Error category
     * @param severity Error severity
     * @param user_message User-friendly message
     * @param technical_details Technical details for debugging
     */
    static void ReportError(ErrorCategory category, ErrorSeverity severity,
                           const std::string& user_message,
                           const std::string& technical_details = "");

    /**
     * @brief Report a fatal error (logs and queues for UI)
     */
    static void ReportFatal(ErrorCategory category,
                           const std::string& user_message,
                           const std::string& technical_details = "");

    /**
     * @brief Report a regular error
     */
    static void ReportError(ErrorCategory category,
                           const std::string& user_message,
                           const std::string& technical_details = "");

    /**
     * @brief Report a warning
     */
    static void ReportWarning(ErrorCategory category,
                             const std::string& user_message,
                             const std::string& technical_details = "");

    /**
     * @brief Check if there are pending errors to display
     */
    static bool HasPendingErrors();

    /**
     * @brief Get all pending errors and clear the queue
     * @return Vector of error reports
     */
    static std::vector<ErrorReport> GetPendingErrors();

    /**
     * @brief Get the last error (for quick checks)
     */
    static ErrorReport GetLastError();

    /**
     * @brief Clear all pending errors without processing
     */
    static void ClearErrors();

    /**
     * @brief Get string representation of error category
     */
    static std::string CategoryToString(ErrorCategory category);

    /**
     * @brief Get string representation of error severity
     */
    static std::string SeverityToString(ErrorSeverity severity);

    /**
     * @brief Get current timestamp as a formatted string
     */
    static std::string GetTimestamp();

private:
    static std::mutex s_mutex;
    static std::vector<ErrorReport> s_error_queue;
    static constexpr size_t MAX_QUEUE_SIZE = 100;
};

} // namespace utils
