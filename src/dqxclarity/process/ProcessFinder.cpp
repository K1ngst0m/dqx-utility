#include "ProcessFinder.hpp"
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
        info.pid = proc.pid;
        info.name = proc.name;
        info.exe_path = proc.path;

#ifdef _WIN32
        info.is_wine_process = false;
#else
        info.is_wine_process = (proc.path.find("wine") != std::string::npos ||
                                proc.path.find(".exe") != std::string::npos);
#endif

        result.push_back(info);
    }

    return result;
}

std::vector<libmem::Pid> ProcessFinder::FindByName(const std::string& name, bool case_sensitive)
{
    auto processes = libmem::EnumProcesses();
    if (!processes)
    {
        return {};
    }

    std::vector<libmem::Pid> matching_pids;
    std::string search_name = case_sensitive ? name : ToLower(name);

    for (const auto& proc : *processes)
    {
        std::string proc_name = case_sensitive ? proc.name : ToLower(proc.name);

        if (proc_name == search_name)
        {
            matching_pids.push_back(proc.pid);
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
                    matching_pids.push_back(proc.pid);
                }
            }
        }
    }

    return matching_pids;
}

std::vector<libmem::Pid> ProcessFinder::FindByExePath(const std::string& path)
{
    auto processes = libmem::EnumProcesses();
    if (!processes)
    {
        return {};
    }

    std::vector<libmem::Pid> matching_pids;

    for (const auto& proc : *processes)
    {
        if (proc.path == path)
        {
            matching_pids.push_back(proc.pid);
        }
    }

    return matching_pids;
}

std::optional<ProcessInfo> ProcessFinder::GetProcessInfo(libmem::Pid pid)
{
    auto process = libmem::GetProcess(pid);
    if (!process)
    {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = process->pid;
    info.name = process->name;
    info.exe_path = process->path;

#ifdef _WIN32
    info.is_wine_process = false;
#else
    info.is_wine_process = (process->path.find("wine") != std::string::npos ||
                            process->path.find(".exe") != std::string::npos);
#endif

    return info;
}

bool ProcessFinder::IsWineProcess(libmem::Pid pid)
{
#ifdef _WIN32
    return false;
#else
    auto process = libmem::GetProcess(pid);
    if (!process)
    {
        return false;
    }

    return (process->path.find("wine") != std::string::npos ||
            process->path.find(".exe") != std::string::npos);
#endif
}

} // namespace dqxclarity
