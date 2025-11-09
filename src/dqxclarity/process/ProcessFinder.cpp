#include "ProcessFinder.hpp"
#include <libmem/libmem.hpp>
#include <algorithm>
#include <cctype>

namespace dqxclarity
{

std::string ProcessFinder::ToLower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

std::vector<ProcessInfo> ProcessFinder::FindAll()
{
    auto processes = libmem::EnumProcesses();
    if (!processes)
    {
        return {};
    }

    std::vector<ProcessInfo> result;
    result.reserve(processes->size());

    for (const auto& proc : *processes)
    {
        ProcessInfo info;
        info.pid = static_cast<pid_t>(proc.pid);
        info.name = proc.name;
        info.exe_path = proc.path;

#ifdef _WIN32
        info.is_wine_process = false;
#else
        info.is_wine_process =
            (proc.path.find("wine") != std::string::npos || proc.path.find(".exe") != std::string::npos);
#endif

        result.push_back(info);
    }

    return result;
}

std::vector<pid_t> ProcessFinder::FindByName(const std::string& name, bool case_sensitive)
{
    auto processes = libmem::EnumProcesses();
    if (!processes)
    {
        return {};
    }

    std::vector<pid_t> matching_pids;
    std::string search_name = case_sensitive ? name : ToLower(name);

    for (const auto& proc : *processes)
    {
        std::string proc_name = case_sensitive ? proc.name : ToLower(proc.name);

        if (proc_name == search_name)
        {
            matching_pids.push_back(static_cast<pid_t>(proc.pid));
            continue;
        }

        if (!proc.path.empty())
        {
            size_t last_slash = proc.path.find_last_of('/');
            if (last_slash == std::string::npos)
            {
                last_slash = proc.path.find_last_of('\\');
            }

            if (last_slash != std::string::npos)
            {
                std::string exe_name = proc.path.substr(last_slash + 1);
                std::string compare_exe = case_sensitive ? exe_name : ToLower(exe_name);

                if (compare_exe == search_name)
                {
                    matching_pids.push_back(static_cast<pid_t>(proc.pid));
                }
            }
        }
    }

    return matching_pids;
}

std::vector<pid_t> ProcessFinder::FindByExePath(const std::string& path)
{
    auto processes = libmem::EnumProcesses();
    if (!processes)
    {
        return {};
    }

    std::vector<pid_t> matching_pids;

    for (const auto& proc : *processes)
    {
        if (proc.path == path)
        {
            matching_pids.push_back(static_cast<pid_t>(proc.pid));
        }
    }

    return matching_pids;
}

std::optional<ProcessInfo> ProcessFinder::GetProcessInfo(pid_t pid)
{
    auto process = libmem::GetProcess(static_cast<libmem::Pid>(pid));
    if (!process)
    {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = static_cast<pid_t>(process->pid);
    info.name = process->name;
    info.exe_path = process->path;

#ifdef _WIN32
    info.is_wine_process = false;
#else
    info.is_wine_process =
        (process->path.find("wine") != std::string::npos || process->path.find(".exe") != std::string::npos);
#endif

    return info;
}

bool ProcessFinder::IsProcessAlive(pid_t pid)
{
    auto process = libmem::GetProcess(static_cast<libmem::Pid>(pid));
    if (!process)
        return false;

    return libmem::IsProcessAlive(&*process);
}

pid_t ProcessFinder::GetCurrentProcessId()
{
    auto process = libmem::GetProcess();
    if (!process)
        return 0;
    
    return static_cast<pid_t>(process->pid);
}

std::filesystem::path ProcessFinder::GetRuntimeDirectory()
{
    auto current_process = libmem::GetProcess();
    if (!current_process)
        return {};
    
    auto exe_dir = std::filesystem::path(current_process->path).parent_path();
    auto runtime_dir = exe_dir / ".dqxu-runtime";
    
    std::error_code ec;
    std::filesystem::create_directories(runtime_dir, ec);
    
    return runtime_dir;
}

bool ProcessFinder::IsWineProcess(pid_t pid)
{
#ifdef _WIN32
    return false;
#else
    auto process = libmem::GetProcess(static_cast<libmem::Pid>(pid));
    if (!process)
    {
        return false;
    }

    return (process->path.find("wine") != std::string::npos || process->path.find(".exe") != std::string::npos);
#endif
}

} // namespace dqxclarity