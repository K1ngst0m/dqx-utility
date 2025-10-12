#pragma once

#include <string>
#include <memory>
#include <vector>

namespace dqxclarity { struct DialogMessage; struct QuestMessage; enum class Status; struct Config; }

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

    // Stop hook and terminate background monitoring (used during shutdown)
    void shutdown();

    // Current status
    DQXClarityStatus getStatus() const;

    // Human-readable status
    std::string getStatusString() const;

    // Check if DQXGame.exe is running
    bool isDQXGameRunning() const;

    // Copy messages with seq > since_seq into out (non-destructive snapshot)
    bool copyDialogsSince(std::uint64_t since_seq, std::vector<dqxclarity::DialogMessage>& out) const;

    bool getLatestQuest(dqxclarity::QuestMessage& out) const;

    // Expose engine stage to guard UI actions
    dqxclarity::Status getEngineStage() const;


    // (No runtime engine tweak APIs exposed in UI)

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
