#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace utils
{

// Cross-platform utilities for process management
class ProcessUtils
{
public:
    // Get the absolute path to the current executable
    static std::filesystem::path GetExecutablePath();

    // Launch a process with optional arguments
    // Returns true on success, false on failure
    // If detached=true, process runs independently after parent exits
    static bool LaunchProcess(const std::filesystem::path& exePath, const std::vector<std::string>& args,
                              bool detached = true);

    // Launch a process with stdin content piped to it
    // The stdinContent will be written to the process's stdin stream
    // The parent process must remain alive until all content is written
    // Returns true on success, false on failure
    static bool LaunchProcessWithStdin(const std::filesystem::path& exePath, const std::vector<std::string>& args,
                                       const std::string& stdinContent, bool detached = true);
};

} // namespace utils
