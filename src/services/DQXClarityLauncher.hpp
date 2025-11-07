#pragma once

#include <string>
#include <memory>
#include <vector>
#include <atomic>

namespace dqxclarity
{
struct DialogMessage;
struct CornerTextItem;
struct QuestMessage;
struct PlayerInfo;
enum class Status;
struct Config;
} // namespace dqxclarity

enum class DQXClarityStatus
{
    Stopped,
    Running,
    Connected,
    Disconnected
};

// In-process controller for dqxclarity
class GlobalStateManager;

class DQXClarityLauncher
{
public:
    DQXClarityLauncher();
    ~DQXClarityLauncher();

    void lateInitialize(GlobalStateManager& global_state);

    bool reinitialize(GlobalStateManager& global_state);

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

    // Last error emitted by the engine (empty if none)
    std::string getLastErrorMessage() const;

    // Check if DQXGame.exe is running
    bool isDQXGameRunning() const;

    // Copy messages with seq > since_seq into out (non-destructive snapshot)
    bool copyDialogsSince(std::uint64_t since_seq, std::vector<dqxclarity::DialogMessage>& out) const;
    bool copyCornerTextSince(std::uint64_t since_seq, std::vector<dqxclarity::CornerTextItem>& out) const;

    bool getLatestQuest(dqxclarity::QuestMessage& out) const;
    bool getLatestPlayer(dqxclarity::PlayerInfo& out) const;

    // Expose engine stage to guard UI actions
    dqxclarity::Status getEngineStage() const;

    // (No runtime engine tweak APIs exposed in UI)

private:
    struct Impl;
    static std::atomic<Impl*> s_active_impl;
    static void CrashCleanupThunk();
    std::unique_ptr<Impl> pimpl_;
};
