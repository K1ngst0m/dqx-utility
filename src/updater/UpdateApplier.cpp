#include "UpdateApplier.hpp"

#include <plog/Log.h>

#include <filesystem>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace updater
{

UpdateApplier::UpdateApplier(const std::string& appDir)
    : appDir_(appDir)
{
}

bool UpdateApplier::applyUpdate(const std::string& packagePath, const std::string& configTemplatePath,
                                ApplyCallback callback, std::string& outError)
{
#ifdef _WIN32
    // Get current executable path
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
    {
        outError = "Failed to get executable path";
        PLOG_ERROR << outError;
        if (callback) callback(false, outError);
        return false;
    }

    // Build command line for updater subprocess
    std::string cmdLine = "\"" + std::string(exePath) + "\" --updater-mode \"" + packagePath + "\" \"" + appDir_ + "\"";

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW; // Show window so user can see update progress

    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
    {
        outError = "Failed to launch updater process";
        PLOG_ERROR << outError;
        if (callback) callback(false, outError);
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    PLOG_INFO << "Updater subprocess launched, application will exit";

    if (callback)
    {
        callback(true, "Update process started");
    }

    return true;
#else
    outError = "Update not implemented for this platform";
    if (callback) callback(false, outError);
    return false;
#endif
}

// This function is called when the app is launched with --updater-mode
bool UpdateApplier::performUpdate(const std::string& packagePath, const std::string& targetDir)
{
#ifdef _WIN32
    PLOG_INFO << "Updater mode: Waiting for main process to exit...";

    // Wait for parent process to exit (up to 30 seconds)
    for (int i = 0; i < 30; ++i)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Check if any dqx-utility.exe is still running
        bool found = false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(snapshot, &pe32))
            {
                do
                {
                    if (_stricmp(pe32.szExeFile, "dqx-utility.exe") == 0 &&
                        pe32.th32ProcessID != GetCurrentProcessId())
                    {
                        found = true;
                        break;
                    }
                } while (Process32Next(snapshot, &pe32));
            }
            CloseHandle(snapshot);
        }

        if (!found)
        {
            PLOG_INFO << "Main process exited";
            break;
        }
    }

    // Change to target directory
    if (!SetCurrentDirectoryA(targetDir.c_str()))
    {
        PLOG_ERROR << "Failed to change to target directory: " << targetDir;
        MessageBoxA(nullptr, "Update failed: Cannot access application directory", "DQX Utility Updater", MB_OK | MB_ICONERROR);
        return false;
    }

    PLOG_INFO << "Creating backup...";
    // Create backup
    fs::remove_all("backup");
    fs::create_directory("backup");
    if (fs::exists("dqx-utility.exe"))
        fs::copy_file("dqx-utility.exe", "backup/dqx-utility.exe", fs::copy_options::overwrite_existing);
    if (fs::exists("SDL3.dll"))
        fs::copy_file("SDL3.dll", "backup/SDL3.dll", fs::copy_options::overwrite_existing);
    if (fs::exists("assets"))
        fs::copy("assets", "backup/assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    PLOG_INFO << "Extracting update from: " << packagePath;
    // Extract package using tar
    std::string tarCmd = "tar -xf \"" + packagePath + "\" --exclude=config.toml";
    int result = system(tarCmd.c_str());

    if (result != 0)
    {
        PLOG_ERROR << "Update extraction failed, restoring backup...";

        // Restore backup
        if (fs::exists("backup/dqx-utility.exe"))
            fs::copy_file("backup/dqx-utility.exe", "dqx-utility.exe", fs::copy_options::overwrite_existing);
        if (fs::exists("backup/SDL3.dll"))
            fs::copy_file("backup/SDL3.dll", "SDL3.dll", fs::copy_options::overwrite_existing);
        if (fs::exists("backup/assets"))
        {
            fs::remove_all("assets");
            fs::copy("backup/assets", "assets", fs::copy_options::recursive);
        }

        MessageBoxA(nullptr, "Update extraction failed! Backup restored.", "DQX Utility Updater", MB_OK | MB_ICONERROR);
        return false;
    }

    PLOG_INFO << "Update completed successfully!";

    // Clean up
    fs::remove(packagePath);
    fs::remove_all("backup");

    // Restart application
    PLOG_INFO << "Restarting application...";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA("dqx-utility.exe", nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return true;
#else
    return false;
#endif
}

} // namespace updater
