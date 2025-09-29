#pragma once

#include <string>

class ProcessDetector
{
public:
    static bool isProcessRunning(const std::string& processName);

private:
#ifdef _WIN32
    static bool isProcessRunningWindows(const std::string& processName);
#else
    static bool isProcessRunningUnix(const std::string& processName);
#endif
};