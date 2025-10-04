#pragma once

#include <string>
#include <memory>
#include <vector>

namespace dqxclarity { struct DialogMessage; }

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

    // Copy messages with seq > since_seq into out (non-destructive snapshot)
    bool copyDialogsSince(std::uint64_t since_seq, std::vector<dqxclarity::DialogMessage>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
