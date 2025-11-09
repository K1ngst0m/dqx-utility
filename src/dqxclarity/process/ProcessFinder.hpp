#pragma once

#include "ProcessTypes.hpp"

#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace dqxclarity
{

struct ProcessInfo
{
    pid_t pid;
    std::string name;
    std::string exe_path;
    bool is_wine_process;
};

class ProcessFinder
{
public:
    static std::vector<ProcessInfo> FindAll();

    static std::vector<pid_t> FindByName(const std::string& name, bool case_sensitive = false);

    static std::vector<pid_t> FindByExePath(const std::string& path);

    static std::optional<ProcessInfo> GetProcessInfo(pid_t pid);
    
    static bool IsProcessAlive(pid_t pid);
    
    static pid_t GetCurrentProcessId();
    
    static std::filesystem::path GetRuntimeDirectory();

    static bool IsWineProcess(pid_t pid);

private:
    static std::string ToLower(const std::string& str);
};

} // namespace dqxclarity
