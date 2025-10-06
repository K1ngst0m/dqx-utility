#include "ProcessDetector.hpp"
#include "WineDetector.hpp"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#else
#include <filesystem>
#include <fstream>
#include <algorithm>
#endif

bool ProcessDetector::isProcessRunning(const std::string& processName)
{
    if (processName.empty())
        return false;

#ifdef _WIN32
    return isProcessRunningWindows(processName);
#else
    return isProcessRunningUnix(processName);
#endif
}

#ifdef _WIN32
bool ProcessDetector::isProcessRunningWindows(const std::string& processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);

    if (!Process32First(snapshot, &entry))
    {
        CloseHandle(snapshot);
        return false;
    }

    std::string targetName = processName;
    std::transform(targetName.begin(), targetName.end(), targetName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    do
    {
        std::string currentName = entry.szExeFile;
        std::transform(currentName.begin(), currentName.end(), currentName.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (currentName == targetName)
        {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return false;
}
#else
bool ProcessDetector::isProcessRunningUnix(const std::string& processName)
{
    std::filesystem::path proc_dir("/proc");
    
    if (!std::filesystem::exists(proc_dir))
        return false;

    for (const auto& entry : std::filesystem::directory_iterator(proc_dir))
    {
        if (!entry.is_directory())
            continue;

        std::string dirname = entry.path().filename().string();
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(), ::isdigit))
            continue;

        std::filesystem::path comm_path = entry.path() / "comm";
        std::ifstream comm_file(comm_path);
        if (!comm_file.is_open())
            continue;

        std::string current_name;
        if (std::getline(comm_file, current_name))
        {
            if (current_name == processName)
                return true;
        }
    }
    return false;
}
#endif

std::optional<WineEnvironment> ProcessDetector::detectWineEnvironment(const std::string& processName)
{
#ifdef _WIN32
    (void)processName;
    // Wine detection not applicable on Windows
    return std::nullopt;
#else
    return WineDetector::detectWineEnvironment(processName);
#endif
}
