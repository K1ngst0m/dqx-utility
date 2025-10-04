#pragma once

#include <vector>
#include <string>
#include <optional>

#ifdef _WIN32
    #include <windows.h>
    using pid_t = DWORD;
#else
    #include <sys/types.h>
#endif

namespace dqxclarity {

struct ProcessInfo {
    pid_t pid;
    std::string name;
    std::string exe_path;
    bool is_wine_process;
};

class ProcessFinder {
public:
    static std::vector<ProcessInfo> FindAll();

    static std::vector<pid_t> FindByName(const std::string& name, bool case_sensitive = false);

    static std::vector<pid_t> FindByExePath(const std::string& path);

    static std::optional<ProcessInfo> GetProcessInfo(pid_t pid);

    static bool IsWineProcess(pid_t pid);

private:
    static std::string ReadProcFile(pid_t pid, const std::string& filename);

    static std::string GetProcessName(pid_t pid);

    static std::string GetProcessExePath(pid_t pid);

    static std::vector<pid_t> EnumerateProcesses();

    static std::string ToLower(const std::string& str);
};

} // namespace dqxclarity
