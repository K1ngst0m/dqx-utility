#include "ProcessDetector.hpp"
#include "WineDetector.hpp"
#include "../utils/ErrorReporter.hpp"

#include <plog/Log.h>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#else
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unistd.h>
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

bool ProcessDetector::isAnotherDQXU(const std::string& processName)
{
    if (processName.empty())
        return false;

#ifdef _WIN32
    return isAnotherDQXUWindows(processName);
#else
    return isAnotherDQXUUnix(processName);
#endif
}

#ifdef _WIN32
namespace {
    std::atomic<bool> g_snapshot_warning_reported{false};
    std::atomic<bool> g_process_iter_warning_reported{false};
}

bool ProcessDetector::isProcessRunningWindows(const std::string& processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        if (!g_snapshot_warning_reported.exchange(true))
        {
            DWORD err = GetLastError();
            PLOG_WARNING << "CreateToolhelp32Snapshot failed: " << err;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan failed",
                std::string("CreateToolhelp32Snapshot error ") + std::to_string(err));
        }
        return false;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);

    if (!Process32First(snapshot, &entry))
    {
        CloseHandle(snapshot);
        if (!g_process_iter_warning_reported.exchange(true))
        {
            DWORD err = GetLastError();
            PLOG_WARNING << "Process32First failed: " << err;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan failed",
                std::string("Process32First error ") + std::to_string(err));
        }
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

bool ProcessDetector::isAnotherDQXUWindows(const std::string& processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        if (!g_snapshot_warning_reported.exchange(true))
        {
            DWORD err = GetLastError();
            PLOG_WARNING << "CreateToolhelp32Snapshot failed: " << err;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan failed",
                std::string("CreateToolhelp32Snapshot error ") + std::to_string(err));
        }
        return false;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);

    if (!Process32First(snapshot, &entry))
    {
        CloseHandle(snapshot);
        if (!g_process_iter_warning_reported.exchange(true))
        {
            DWORD err = GetLastError();
            PLOG_WARNING << "Process32First failed: " << err;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan failed",
                std::string("Process32First error ") + std::to_string(err));
        }
        return false;
    }

    std::string targetName = processName;
    std::transform(targetName.begin(), targetName.end(), targetName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    DWORD current_pid = GetCurrentProcessId();

    do
    {
        std::string currentName = entry.szExeFile;
        std::transform(currentName.begin(), currentName.end(), currentName.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (currentName == targetName && entry.th32ProcessID != current_pid)
        {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return false;
}
#else
namespace {
    std::atomic<bool> g_procdir_warning_reported{false};
}

bool ProcessDetector::isProcessRunningUnix(const std::string& processName)
{
    std::filesystem::path proc_dir("/proc");
    
    if (!std::filesystem::exists(proc_dir))
    {
        if (!g_procdir_warning_reported.exchange(true))
        {
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan unavailable",
                "/proc directory not found");
        }
        return false;
    }

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

bool ProcessDetector::isAnotherDQXUUnix(const std::string& processName)
{
    std::filesystem::path proc_dir("/proc");

    if (!std::filesystem::exists(proc_dir))
    {
        if (!g_procdir_warning_reported.exchange(true))
        {
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection,
                "Process scan unavailable",
                "/proc directory not found");
        }
        return false;
    }

    pid_t current_pid = getpid();

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
            {
                pid_t pid_value = static_cast<pid_t>(std::strtol(dirname.c_str(), nullptr, 10));
                if (pid_value != current_pid)
                    return true;
            }
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
