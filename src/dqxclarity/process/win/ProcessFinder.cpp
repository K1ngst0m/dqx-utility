#include "../ProcessFinder.hpp"
#include "../../util/Profile.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <unordered_map>

namespace dqxclarity
{

// Cached process snapshot to avoid repeated CreateToolhelp32Snapshot calls
static std::unordered_map<pid_t, std::string> g_process_cache;
static bool g_cache_valid = false;

static void RefreshProcessCache()
{
    PROFILE_SCOPE_CUSTOM("ProcessFinder.RefreshCache");
    g_process_cache.clear();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(snapshot, &pe))
        {
            do
            {
                if (pe.th32ProcessID > 0)
                {
                    g_process_cache[pe.th32ProcessID] = std::string(pe.szExeFile);
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    g_cache_valid = true;
}

std::vector<pid_t> ProcessFinder::EnumerateProcesses()
{
    if (!g_cache_valid)
    {
        RefreshProcessCache();
    }

    std::vector<pid_t> pids;
    pids.reserve(g_process_cache.size());
    for (const auto& [pid, name] : g_process_cache)
    {
        pids.push_back(pid);
    }

    // Ensure current process is included
    DWORD self = GetCurrentProcessId();
    if (std::find(pids.begin(), pids.end(), static_cast<pid_t>(self)) == pids.end())
    {
        pids.push_back(static_cast<pid_t>(self));
    }
    return pids;
}

std::string ProcessFinder::ReadProcFile(pid_t, const std::string&) { return ""; }

std::string ProcessFinder::GetProcessName(pid_t pid)
{
    if (!g_cache_valid)
    {
        RefreshProcessCache();
    }

    auto it = g_process_cache.find(pid);
    if (it != g_process_cache.end())
    {
        return it->second;
    }

    return "";
}

std::string ProcessFinder::GetProcessExePath(pid_t pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == NULL)
    {
        return "";
    }

    char path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(process, 0, path, &size))
    {
        CloseHandle(process);
        return std::string(path);
    }

    CloseHandle(process);
    return "";
}

bool ProcessFinder::IsWineProcess(pid_t) { return false; }

} // namespace dqxclarity
