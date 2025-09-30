#pragma once

#include <string>
#include <optional>
#include <memory>

struct WineEnvironment;

enum class DQXClarityStatus
{
    Stopped,        // dqxclarity is not running
    Running,        // dqxclarity is running (Windows or Linux without Wine check)
    Connected,      // Linux: dqxclarity running and on same wineserver as DQXGame
    Disconnected    // Linux: dqxclarity running but on different wineserver
};

// Manages launching, stopping, and monitoring dqxclarity (main.py -d)
class DQXClarityLauncher
{
public:
    DQXClarityLauncher();
    ~DQXClarityLauncher();

    // Launch dqxclarity. Returns true on success.
    // On Linux, uses Wine environment from DQXGame.exe if available
    // On Windows, launches directly
    bool launch();

    // Stop dqxclarity gracefully
    // On Linux, uses cleanup script with DQXURUN_ID
    // On Windows, terminates the process
    bool stop();

    // Get current status of dqxclarity
    DQXClarityStatus getStatus() const;

    // Get human-readable status string
    std::string getStatusString() const;

    // Check if DQXGame.exe is running (prerequisite for launch)
    bool isDQXGameRunning() const;

    // Get the current run ID (Linux only, for tracking processes)
    std::string getRunId() const { return run_id_; }

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;

    std::string run_id_;  // Unique ID for this launch session

    // Platform-specific implementations
#ifdef _WIN32
    bool launchWindows();
    bool stopWindows();
    DQXClarityStatus getStatusWindows() const;
#else
    bool launchLinux();
    bool stopLinux();
    DQXClarityStatus getStatusLinux() const;
    
    // Linux-specific helpers
    std::optional<int> findDQXClarityPid() const;
    std::optional<std::string> getWineserverPath(int pid) const;
    bool isOnSameWineserver(const std::string& dqxgame_wineserver, const std::string& dqxc_wineserver) const;
#endif

    // Common helpers
    std::string getProjectRoot() const;
    std::string generateRunId() const;
};