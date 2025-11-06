#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "player_info.hpp"
#include "corner_text.hpp"

namespace dqxclarity
{

enum class Status
{
    Stopped,
    Starting,
    Hooked,
    Stopping,
    Error
};

enum class HookStage
{
    Idle,                    // Engine not started
    AttachingProcess,        // Attaching to game process
    ScanningForNotice,       // Waiting for notice screen pattern
    WaitingForIntegrity,     // Notice found, waiting for integrity check completion
    InstallingHooks,         // Installing memory hooks
    Ready                    // Fully hooked and operational
};

struct EngineState
{
    Status status{ Status::Stopped };
    HookStage hook_stage{ HookStage::Idle };
};

struct QuestMessage;
struct DialogMessage;

struct Config
{
    bool verbose = false;
    bool console_output = false; // kept for parity; not used by the library
    bool defer_dialog_patch = true; // enable initial patch only after first integrity
    bool instruction_safe_steal = true; // compute stolen bytes safely
    int readback_bytes = 16; // how many bytes to log from patch sites
    int proactive_verify_after_enable_ms = 200; // recheck patch post-enable
    bool enable_post_login_heuristics = false; // optional walkthrough heuristic
    // Diagnostic switch: when true, emits detailed integrity scanning diagnostics.
    bool enable_integrity_diagnostics = false;
    // Dialog capture mode: false = auto (hook + memory reader), true = compatibility (memory reader only)
    bool compatibility_mode = false;
    // Hook priority wait time: how long to wait for hook to upgrade memory reader captures (ms)
    int hook_wait_timeout_ms = 200;
};

struct Logger
{
    std::function<void(const std::string&)> info;
    std::function<void(const std::string&)> debug;
    std::function<void(const std::string&)> warn;
    std::function<void(const std::string&)> error;
};

class Engine
{
public:
    Engine();
    ~Engine() noexcept;

    /**
     * @brief Hook startup policy
     * 
     * Controls how the engine waits for readiness before installing hooks:
     * 
     * - DeferUntilIntegrity: Attach → scan for notice screen → wait for integrity → install hooks
     *   Progress: Idle → AttachingProcess → ScanningForNotice → WaitingForIntegrity → InstallingHooks → Ready
     * 
     * - EnableImmediately: Attach → install hooks immediately (skip notice detection)
     *   Progress: Idle → AttachingProcess → InstallingHooks → Ready
     * 
     * The engine internally manages scanner warmup and waits, exposing hook progress via Engine::state().
     */
    enum class StartPolicy
    {
        DeferUntilIntegrity,
        EnableImmediately
    };

    bool initialize(const Config& cfg, Logger loggers = {});
    bool start_hook();
    bool start_hook(StartPolicy policy);
    bool stop_hook();

    Status status() const { return status_; }
    EngineState state() const;
    std::string last_error() const;

    // Drain all available dialog messages into out (single consumer)
    bool drain(std::vector<DialogMessage>& out);
    bool drainCornerText(std::vector<CornerTextItem>& out);

    bool latest_quest(QuestMessage& out) const;
    bool latest_player(PlayerInfo& out) const;
    void update_player_info(PlayerInfo info);

    // Scanner state access
    bool isNoticeScreenVisible() const;
    bool isPostLoginDetected() const;
    bool scanPlayerInfo(PlayerInfo& out);

    // Scanner state listeners
    using NoticeListenerId = std::uint64_t;
    NoticeListenerId addNoticeStateListener(std::function<void(bool)> callback);
    void removeNoticeStateListener(NoticeListenerId id);

    using PostLoginListenerId = std::uint64_t;
    PostLoginListenerId addPostLoginStateListener(std::function<void(bool)> callback);
    void removePostLoginStateListener(PostLoginListenerId id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Status status_ = Status::Stopped;
};

} // namespace dqxclarity
