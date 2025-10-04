#include "ProcessFinder.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <optional>

namespace dqxclarity {

// Platform-specific implementations of these helpers are compiled from
// ProcessFinderWin.cpp or ProcessFinderLinux.cpp:
//   EnumerateProcesses
//   ReadProcFile
//   GetProcessName
//   GetProcessExePath
//   IsWineProcess

std::string ProcessFinder::ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<ProcessInfo> ProcessFinder::FindAll() {
    std::vector<ProcessInfo> processes;
    auto pids = EnumerateProcesses();

    for (pid_t pid : pids) {
        auto info = GetProcessInfo(pid);
        if (info.has_value()) {
            processes.push_back(info.value());
        }
    }

    return processes;
}

std::vector<pid_t> ProcessFinder::FindByName(const std::string& name, bool case_sensitive) {
    std::vector<pid_t> matching_pids;
    auto pids = EnumerateProcesses();

    std::string search_name = case_sensitive ? name : ToLower(name);

    for (pid_t pid : pids) {
        std::string proc_name = GetProcessName(pid);
        if (proc_name.empty()) {
            continue;
        }

        std::string compare_name = case_sensitive ? proc_name : ToLower(proc_name);

        if (compare_name == search_name) {
            matching_pids.push_back(pid);
            continue;
        }

        std::string exe_path = GetProcessExePath(pid);
        if (!exe_path.empty()) {
            size_t last_slash = exe_path.find_last_of('/');
            if (last_slash != std::string::npos) {
                std::string exe_name = exe_path.substr(last_slash + 1);
                std::string compare_exe = case_sensitive ? exe_name : ToLower(exe_name);
                if (compare_exe == search_name) {
                    matching_pids.push_back(pid);
                }
            }
        }
    }

    return matching_pids;
}

std::vector<pid_t> ProcessFinder::FindByExePath(const std::string& path) {
    std::vector<pid_t> matching_pids;
    auto pids = EnumerateProcesses();

    for (pid_t pid : pids) {
        std::string exe_path = GetProcessExePath(pid);
        if (exe_path == path) {
            matching_pids.push_back(pid);
        }
    }

    return matching_pids;
}

std::optional<ProcessInfo> ProcessFinder::GetProcessInfo(pid_t pid) {
    std::string name = GetProcessName(pid);
    if (name.empty()) {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = pid;
    info.name = name;
    info.exe_path = GetProcessExePath(pid);
    info.is_wine_process = IsWineProcess(pid);

    return info;
}

} // namespace dqxclarity
