#include "../ProcessFinder.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>

namespace dqxclarity
{

std::vector<pid_t> ProcessFinder::EnumerateProcesses()
{
    std::vector<pid_t> pids;
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
                    pids.push_back(pe.th32ProcessID);
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);
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
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return "";
    }

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &pe))
    {
        do
        {
            if (pe.th32ProcessID == pid)
            {
                CloseHandle(snapshot);
                return std::string(pe.szExeFile);
            }
        } while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);
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
