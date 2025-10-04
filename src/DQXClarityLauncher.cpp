#include "DQXClarityLauncher.hpp"
#include "ProcessDetector.hpp"
#include "WineDetector.hpp"

#include <plog/Log.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

// Private implementation details
struct DQXClarityLauncher::Impl
{
#ifdef _WIN32
    HANDLE process_handle = nullptr;
    DWORD process_id = 0;
#else
    pid_t process_id = 0;
#endif
};

DQXClarityLauncher::DQXClarityLauncher()
    : pimpl_(std::make_unique<Impl>())
{
}

DQXClarityLauncher::~DQXClarityLauncher()
{
#ifdef _WIN32
    if (pimpl_->process_handle)
        CloseHandle(pimpl_->process_handle);
#endif
}

bool DQXClarityLauncher::isDQXGameRunning() const
{
    return ProcessDetector::isProcessRunning("DQXGame.exe");
}

bool DQXClarityLauncher::launch()
{
    if (!isDQXGameRunning())
    {
        PLOG_WARNING << "Cannot launch dqxclarity: DQXGame.exe is not running";
        return false;
    }

    if (getStatus() != DQXClarityStatus::Stopped)
    {
        PLOG_WARNING << "dqxclarity is already running";
        return false;
    }

#ifdef _WIN32
    return launchWindows();
#else
    return launchLinux();
#endif
}

bool DQXClarityLauncher::stop()
{
#ifdef _WIN32
    return stopWindows();
#else
    return stopLinux();
#endif
}

DQXClarityStatus DQXClarityLauncher::getStatus() const
{
#ifdef _WIN32
    return getStatusWindows();
#else
    return getStatusLinux();
#endif
}

std::string DQXClarityLauncher::getStatusString() const
{
    switch (getStatus())
    {
        case DQXClarityStatus::Stopped:
            return "Stopped";
        case DQXClarityStatus::Running:
            return "Running";
        case DQXClarityStatus::Connected:
            return "OK";
        case DQXClarityStatus::Disconnected:
            return "Disconnected";
        default:
            return "Unknown";
    }
}

std::string DQXClarityLauncher::getProjectRoot() const
{
    // Get executable directory and go up to project root
    std::filesystem::path exe_path = std::filesystem::current_path();
    
    // Look for dqxclarity directory to confirm project root
    for (int i = 0; i < 3; ++i)
    {
        auto dqxc_path = exe_path / "dqxclarity";
        if (std::filesystem::exists(dqxc_path) && std::filesystem::is_directory(dqxc_path))
        {
            return exe_path.string();
        }
        exe_path = exe_path.parent_path();
    }
    
    PLOG_ERROR << "Could not find project root (dqxclarity directory)";
    return "";
}

std::string DQXClarityLauncher::generateRunId() const
{
    // Generate timestamp-based ID as fallback
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(ms);
}

#ifdef _WIN32
// ========== Windows Implementation ==========

bool DQXClarityLauncher::launchWindows()
{
    std::string project_root = getProjectRoot();
    if (project_root.empty())
        return false;

    std::string python_path = project_root + "\\venv\\Scripts\\python.exe";
    std::string main_py = project_root + "\\dqxclarity\\main.py";
    
    if (!std::filesystem::exists(python_path))
    {
        PLOG_ERROR << "Python not found at: " << python_path;
        return false;
    }
    
    if (!std::filesystem::exists(main_py))
    {
        PLOG_ERROR << "main.py not found at: " << main_py;
        return false;
    }

    // Build command line: python.exe main.py -d
    std::string cmdline = "\"" + python_path + "\" \"" + main_py + "\" -d";
    
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide console window
    
    PROCESS_INFORMATION pi = {};
    
    PLOG_INFO << "Launching dqxclarity: " << cmdline;
    
    if (!CreateProcessA(
        nullptr,
        const_cast<char*>(cmdline.c_str()),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        project_root.c_str(),
        &si,
        &pi))
    {
        PLOG_ERROR << "Failed to launch dqxclarity: " << GetLastError();
        return false;
    }
    
    pimpl_->process_handle = pi.hProcess;
    pimpl_->process_id = pi.dwProcessId;
    CloseHandle(pi.hThread);
    
    PLOG_INFO << "dqxclarity launched successfully (PID: " << pi.dwProcessId << ")";
    return true;
}

bool DQXClarityLauncher::stopWindows()
{
    if (!pimpl_->process_handle)
    {
        PLOG_WARNING << "No process to stop";
        return false;
    }
    
    // Try graceful termination first
    if (TerminateProcess(pimpl_->process_handle, 0))
    {
        WaitForSingleObject(pimpl_->process_handle, 5000);
        CloseHandle(pimpl_->process_handle);
        pimpl_->process_handle = nullptr;
        pimpl_->process_id = 0;
        
        PLOG_INFO << "dqxclarity stopped successfully";
        return true;
    }
    
    PLOG_ERROR << "Failed to stop dqxclarity: " << GetLastError();
    return false;
}

DQXClarityStatus DQXClarityLauncher::getStatusWindows() const
{
    // Check if we have a tracked process
    if (pimpl_->process_handle)
    {
        DWORD exit_code;
        if (GetExitCodeProcess(pimpl_->process_handle, &exit_code))
        {
            if (exit_code == STILL_ACTIVE)
                return DQXClarityStatus::Running;
        }
    }
    
    // Check if pythonw.exe with main.py is running
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return DQXClarityStatus::Stopped;
    
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    
    if (!Process32First(snapshot, &entry))
    {
        CloseHandle(snapshot);
        return DQXClarityStatus::Stopped;
    }
    
    do
    {
        std::string proc_name = entry.szExeFile;
        std::transform(proc_name.begin(), proc_name.end(), proc_name.begin(), ::tolower);
        
        if (proc_name == "python.exe" || proc_name == "pythonw.exe")
        {
            // TODO: Check command line for main.py -d
            // For now, assume any python process is dqxclarity
            CloseHandle(snapshot);
            return DQXClarityStatus::Running;
        }
    } while (Process32Next(snapshot, &entry));
    
    CloseHandle(snapshot);
    return DQXClarityStatus::Stopped;
}

#else
// ========== Linux Implementation ==========

bool DQXClarityLauncher::launchLinux()
{
    std::string project_root = getProjectRoot();
    if (project_root.empty())
        return false;

    // Get Wine environment from DQXGame.exe
    auto wine_env = ProcessDetector::detectWineEnvironment("DQXGame.exe");
    if (!wine_env.has_value())
    {
        PLOG_ERROR << "Could not detect Wine environment for DQXGame.exe";
        return false;
    }
    
    PLOG_INFO << "Using Wine prefix: " << wine_env->wine_prefix;

    // Generate unique run ID
    run_id_ = generateRunId();
    
    std::string script_path = project_root + "/winedev/run_dqxc.sh";
    if (!std::filesystem::exists(script_path))
    {
        PLOG_ERROR << "Launch script not found: " << script_path;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        PLOG_ERROR << "Failed to fork process";
        return false;
    }
    
    if (pid == 0)
    {
        // Child process
        // Set environment variables
        setenv("DQXURUN_ID", run_id_.c_str(), 1);
        setenv("WINEPREFIX", wine_env->wine_prefix.c_str(), 1);
        
        // Redirect stdout/stderr to log file
        std::string log_path = project_root + "/dqxclarity/logs/dqxc_stdout.log";
        (void)freopen(log_path.c_str(), "a", stdout);
        (void)freopen(log_path.c_str(), "a", stderr);
        
        // Execute script
        execl(script_path.c_str(), script_path.c_str(), nullptr);
        
        // If we get here, exec failed
        _exit(1);
    }
    
    // Parent process
    pimpl_->process_id = pid;
    
    // Wait a bit to see if process starts successfully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check if child is still alive
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result != 0)
    {
        PLOG_ERROR << "dqxclarity process failed to start";
        pimpl_->process_id = 0;
        return false;
    }
    
    PLOG_INFO << "dqxclarity launched successfully (PID: " << pid << ", RUN_ID: " << run_id_ << ")";
    return true;
}

bool DQXClarityLauncher::stopLinux()
{
    if (run_id_.empty())
    {
        PLOG_WARNING << "No run ID available for cleanup";
        return false;
    }
    
    std::string project_root = getProjectRoot();
    if (project_root.empty())
        return false;
    
    std::string cleanup_script = project_root + "/winedev/cleanup_dqxc.sh";
    if (!std::filesystem::exists(cleanup_script))
    {
        PLOG_ERROR << "Cleanup script not found: " << cleanup_script;
        return false;
    }
    
    PLOG_INFO << "Stopping dqxclarity with RUN_ID: " << run_id_;
    
    // Execute cleanup script
    std::string cmd = cleanup_script + " " + run_id_;
    int result = system(cmd.c_str());
    
    if (result == 0)
    {
        PLOG_INFO << "dqxclarity stopped successfully";
        run_id_.clear();
        pimpl_->process_id = 0;
        return true;
    }
    
    PLOG_ERROR << "Failed to stop dqxclarity (exit code: " << result << ")";
    return false;
}

DQXClarityStatus DQXClarityLauncher::getStatusLinux() const
{
    // Find dqxclarity process
    auto pid = findDQXClarityPid();
    if (!pid.has_value())
        return DQXClarityStatus::Stopped;
    
    // On Linux, check wineserver compatibility
    if (!isDQXGameRunning())
        return DQXClarityStatus::Running;  // DQXGame not running, can't check wineserver
    
    // Get wineserver paths for both processes
    auto dqxgame_wineserver = getWineserverPath(-1);  // -1 means find DQXGame.exe first
    auto dqxc_wineserver = getWineserverPath(pid.value());
    
    if (!dqxgame_wineserver.has_value() || !dqxc_wineserver.has_value())
        return DQXClarityStatus::Running;  // Can't determine wineserver
    
    if (isOnSameWineserver(dqxgame_wineserver.value(), dqxc_wineserver.value()))
        return DQXClarityStatus::Connected;
    else
        return DQXClarityStatus::Disconnected;
}

std::optional<int> DQXClarityLauncher::findDQXClarityPid() const
{
    if (run_id_.empty())
        return std::nullopt;
        
    std::filesystem::path proc_dir("/proc");
    if (!std::filesystem::exists(proc_dir))
        return std::nullopt;
    
    for (const auto& entry : std::filesystem::directory_iterator(proc_dir))
    {
        if (!entry.is_directory())
            continue;
        
        std::string dirname = entry.path().filename().string();
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(), ::isdigit))
            continue;
        
        int pid = std::stoi(dirname);
        
        std::filesystem::path environ_path = entry.path() / "environ";
        std::ifstream environ_file(environ_path);
        if (environ_file.is_open())
        {
            std::string environ_content((std::istreambuf_iterator<char>(environ_file)),
                                      std::istreambuf_iterator<char>());
            if (environ_content.find("DQXURUN_ID=" + run_id_) != std::string::npos)
            {
                return pid;
            }
        }
    }
    
    return std::nullopt;
}

std::optional<std::string> DQXClarityLauncher::getWineserverPath(int pid) const
{
    if (pid < 0)
    {
        // Find DQXGame.exe first
        auto wine_env = ProcessDetector::detectWineEnvironment("DQXGame.exe");
        if (!wine_env.has_value())
            return std::nullopt;
        
        // Wineserver path is typically prefix/.wine.server
        std::string wineserver_dir = wine_env->wine_prefix;
        if (wineserver_dir.back() == '/')
            wineserver_dir.pop_back();
        
        return wineserver_dir;
    }
    
    // Read WINEPREFIX from process environment
    std::filesystem::path environ_path = std::filesystem::path("/proc") / std::to_string(pid) / "environ";
    std::ifstream environ_file(environ_path, std::ios::binary);
    
    if (!environ_file.is_open())
        return std::nullopt;
    
    std::string buffer((std::istreambuf_iterator<char>(environ_file)), std::istreambuf_iterator<char>());
    
    // Find WINEPREFIX
    size_t pos = buffer.find("WINEPREFIX=");
    if (pos == std::string::npos)
        return std::nullopt;
    
    pos += 11;  // Skip "WINEPREFIX="
    size_t end = buffer.find('\0', pos);
    if (end == std::string::npos)
        return std::nullopt;
    
    std::string wineprefix = buffer.substr(pos, end - pos);
    if (wineprefix.back() == '/')
        wineprefix.pop_back();
    
    return wineprefix;
}

bool DQXClarityLauncher::isOnSameWineserver(const std::string& dqxgame_wineserver, const std::string& dqxc_wineserver) const
{
    // Compare wine prefix paths - they should be identical
    return dqxgame_wineserver == dqxc_wineserver;
}

#endif
