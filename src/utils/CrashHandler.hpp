#pragma once

#include <atomic>

namespace utils
{

class CrashHandler
{
public:
    static void Initialize();
    static void SetContext(const char* operation);
    static void RegisterFatalFlag(std::atomic<bool>* flag);
    static void RegisterFatalCleanup(void (*fn)());
};

} // namespace utils
