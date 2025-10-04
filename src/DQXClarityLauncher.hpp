#pragma once

#include <string>
#include <memory>

enum class DQXClarityStatus
{
    Stopped,
    Running,
    Connected,
    Disconnected
};

// In-process controller for dqxclarity
class DQXClarityLauncher
{
public:
    DQXClarityLauncher();
    ~DQXClarityLauncher();

    // Start hook
    bool launch();

    // Stop hook
    bool stop();

    // Current status
    DQXClarityStatus getStatus() const;

    // Human-readable status
    std::string getStatusString() const;

    // Check if DQXGame.exe is running
    bool isDQXGameRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
