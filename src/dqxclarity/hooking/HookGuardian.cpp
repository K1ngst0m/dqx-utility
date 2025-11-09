#include "HookGuardian.hpp"
#include "HookRegistry.hpp"
#include "../process/ProcessFinder.hpp"

#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dqxclarity
{
namespace persistence
{

// Dev toggle for debugging, set to false to disable guardian
static constexpr bool kEnableGuardian = true;

std::string HookGuardian::GetHeartbeatPath()
{
    return (ProcessFinder::GetRuntimeDirectory() / "guardian_heartbeat.tmp").string();
}

std::string HookGuardian::GetShutdownSignalPath()
{
    return (ProcessFinder::GetRuntimeDirectory() / "guardian_shutdown.tmp").string();
}

bool HookGuardian::IsDQXGameRunning()
{
    auto pids = ProcessFinder::FindByName("DQXGame.exe", false);
    return !pids.empty();
}

bool HookGuardian::IsMainProcessAlive()
{
    auto heartbeat_path = GetHeartbeatPath();
    
    if (!std::filesystem::exists(heartbeat_path))
    {
        return false;
    }
    
    std::ifstream file(heartbeat_path);
    if (!file)
    {
        return false;
    }
    
    uint64_t pid = 0;
    int64_t timestamp_ms = 0;
    file >> pid >> timestamp_ms;
    
    if (pid == 0)
    {
        return false;
    }
    
    if (!ProcessFinder::IsProcessAlive(static_cast<pid_t>(pid)))
    {
        return false;
    }
    
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    auto elapsed_ms = now_ms - timestamp_ms;
    return elapsed_ms < kHeartbeatTimeout.count() * 1000;
}

void HookGuardian::UpdateHeartbeat()
{
    if (!kEnableGuardian)
    {
        return;
    }
    
    auto heartbeat_path = GetHeartbeatPath();
    
    std::ofstream file(heartbeat_path, std::ios::trunc);
    if (!file)
    {
        return;
    }
    
    uint64_t pid = static_cast<uint64_t>(ProcessFinder::GetCurrentProcessId());
    
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    file << pid << " " << now_ms;
}

void HookGuardian::SignalShutdown()
{
    if (!kEnableGuardian)
    {
        return;
    }
    
    auto shutdown_path = GetShutdownSignalPath();
    std::ofstream file(shutdown_path);
    
    std::filesystem::remove(GetHeartbeatPath());
}

bool HookGuardian::StartGuardian()
{
    if (!kEnableGuardian)
    {
        return true;
    }
    
    std::filesystem::remove(GetShutdownSignalPath());
    UpdateHeartbeat();
    
#ifdef _WIN32
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    
    std::wstring cmd_line = std::wstring(L"\"") + exe_path + L"\" --guardian-internal-mode";
    
    if (!CreateProcessW(
        exe_path,
        cmd_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        nullptr,
        nullptr,
        &si,
        &pi))
    {
        return false;
    }
    
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    
#else
    pid_t pid = fork();
    
    if (pid < 0)
    {
        return false;
    }
    
    if (pid == 0)
    {
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len == -1)
        {
            _exit(1);
        }
        exe_path[len] = '\0';
        
        execl(exe_path, exe_path, "--guardian-internal-mode", nullptr);
        _exit(1);
    }
#endif
    
    return true;
}

int HookGuardian::RunGuardianLoop()
{
    if (!kEnableGuardian)
    {
        return 0;
    }
    
    auto last_game_check = std::chrono::steady_clock::now();
    auto last_main_check = std::chrono::steady_clock::now();
    bool cleanup_attempted = false;
    
    while (true)
    {
        auto now = std::chrono::steady_clock::now();
        
        // Check if shutdown signal received
        if (std::filesystem::exists(GetShutdownSignalPath()))
        {
            std::filesystem::remove(GetShutdownSignalPath());
            return 0;
        }
        
        // Check DQXGame.exe status every second
        if (now - last_game_check >= kCheckInterval)
        {
            last_game_check = now;
            
            if (!IsDQXGameRunning())
            {
                return 0;
            }
        }
        
        // Check main process status every 500ms
        if (now - last_main_check >= kMainProcessCheckInterval)
        {
            last_main_check = now;
            
            if (!IsMainProcessAlive() && !cleanup_attempted)
            {
                // Main process dead or heartbeat timeout, game still alive
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                // Double check before cleanup
                if (!IsMainProcessAlive() && IsDQXGameRunning())
                {
                    HookRegistry::CheckAndCleanup();
                    cleanup_attempted = true;
                }
                
                return 0;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace persistence
} // namespace dqxclarity
