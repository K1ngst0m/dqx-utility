#pragma once

#include <atomic>

namespace utils
{

/**
 * Crash handler for unhandled exceptions and fatal errors.
 *
 * Captures crashes, generates stack traces via cpptrace, 
 * and writes minidump files (windows).
 * Also intercepts std::terminate() calls.
 */
class CrashHandler
{
public:
    /// Installs exception handlers
    static void Initialize();

    /// Sets thread-local context string to be included in crash reports
    static void SetContext(const char* operation);

    /// Registers an atomic flag to be set to true on fatal error
    static void RegisterFatalFlag(std::atomic<bool>* flag);

    /// Registers a cleanup function to be called before crash termination (e.g., flush buffers)
    static void RegisterFatalCleanup(void (*fn)());
};

} // namespace utils
