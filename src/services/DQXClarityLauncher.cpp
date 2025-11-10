#include "DQXClarityLauncher.hpp"

#include <plog/Log.h>

#include "../config/ConfigManager.hpp"
#include "../ui/GlobalStateManager.hpp"
#include "../ui/Localization.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../utils/CrashHandler.hpp"
#include "../utils/Profile.hpp"

#include "dqxclarity/api/dqxclarity.hpp"
#include "dqxclarity/api/dialog_message.hpp"
#include "dqxclarity/api/corner_text.hpp"
#include "dqxclarity/api/player_info.hpp"
#include "dqxclarity/api/quest_message.hpp"
#include "dqxclarity/process/ProcessFinder.hpp"
#include "DQXClarityService.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <exception>
#include <optional>


// Private implementation details
struct DQXClarityLauncher::Impl
{
    std::unique_ptr<dqxclarity::Engine> engine;
    std::jthread monitor;
    std::atomic<bool> shutdown_called{ false };
    std::mutex engine_mutex;
    bool waiting_delay = false;
    std::chrono::steady_clock::time_point detect_tp{};
    int delay_ms = 5000;

    std::jthread watchdog;
    std::atomic<std::uint64_t> heartbeat_seq{ 0 };
    std::atomic<bool> fatal_signal{ false };
    std::atomic<bool> stop_in_progress{ false };

    // Process state for start policy
    bool process_running_at_start = false;
    bool attempted_auto_start = false;

    // Watchdog suspension: prevents false positives during engine warmup
    std::atomic<bool> monitor_in_start_hook{ false };

    mutable std::mutex error_mutex;
    std::string last_error_message;
    std::atomic<bool> process_warning_reported{ false };

    // Backlog for UI consumers to read with per-window seq cursors
    mutable std::mutex backlog_mutex;
    std::vector<dqxclarity::DialogMessage> backlog;
    static constexpr std::size_t kMaxBacklog = 2048;

    mutable std::mutex corner_text_mutex;
    std::vector<dqxclarity::CornerTextItem> corner_text_backlog;
    static constexpr std::size_t kMaxCornerTextBacklog = 1024;

    mutable std::mutex quest_mutex;
    dqxclarity::QuestMessage latest_quest;
    bool quest_valid = false;

    mutable std::mutex player_mutex;
    dqxclarity::PlayerInfo latest_player;
    bool player_valid = false;

    // Config mirrors
    dqxclarity::Config engine_cfg{};
    bool enable_post_login_heuristics = false;
    bool policy_skip_when_process_running = true;
    int notice_wait_timeout_ms = 0; // 0 = infinite

    bool startHookLocked(dqxclarity::Engine::StartPolicy policy)
    {
        // Signal watchdog that we're entering start_hook (may block during warmup)
        monitor_in_start_hook.store(true, std::memory_order_release);
        
        std::lock_guard<std::mutex> lock(engine_mutex);
        clearLastErrorMessage();
        bool ok = engine->start_hook(policy);
        
        // Clear the flag after start_hook completes
        monitor_in_start_hook.store(false, std::memory_order_release);
        
        if (!ok && last_error_message.empty())
        {
            std::string policy_name =
                (policy == dqxclarity::Engine::StartPolicy::EnableImmediately) ?
                    "EnableImmediately" :
                    (policy == dqxclarity::Engine::StartPolicy::DeferUntilIntegrity ? "DeferUntilIntegrity" :
                                                                                      "Unknown");
            setLastErrorMessage("Failed to start hook (" + policy_name + ").");
        }
        return ok;
    }

    bool stopHookLocked()
    {
        bool expected = false;
        if (!stop_in_progress.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        struct StopReset
        {
            std::atomic<bool>& flag;

            ~StopReset() { flag.store(false, std::memory_order_release); }
        } reset{ stop_in_progress };

        bool ok = true;
        try
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            auto status = engine->status();
            if (status == dqxclarity::Status::Stopped)
            {
                clearLastErrorMessage();
                return true;
            }
            ok = engine->stop_hook();
            if (ok)
            {
                std::lock_guard<std::mutex> qlock(quest_mutex);
                quest_valid = false;
            }
            if (ok)
            {
                std::lock_guard<std::mutex> plock(player_mutex);
                player_valid = false;
                latest_player = dqxclarity::PlayerInfo{};
            }
            if (ok)
            {
                clearLastErrorMessage();
            }
            else if (last_error_message.empty())
            {
                setLastErrorMessage("Failed to stop hook.");
            }
        }
        catch (const std::exception& ex)
        {
            ok = false;
            PLOG_ERROR << "Exception during stopHookLocked: " << ex.what();
            setLastErrorMessage("Exception during stop hook.");
        }
        catch (...)
        {
            ok = false;
            PLOG_ERROR << "Unknown exception during stopHookLocked";
            setLastErrorMessage("Unknown exception during stop hook.");
        }
        return ok;
    }

    void setLastErrorMessage(const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (last_error_message == msg)
            return;
        last_error_message = msg;
        if (!last_error_message.empty())
        {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::MemoryHook, "Clarity hook encountered an error",
                                              last_error_message);
        }
    }

    std::string getLastErrorMessage() const
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        return last_error_message;
    }

    void clearLastErrorMessage()
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error_message.clear();
    }
};

std::atomic<DQXClarityLauncher::Impl*> DQXClarityLauncher::s_active_impl{ nullptr };

void DQXClarityLauncher::CrashCleanupThunk()
{
    if (auto* impl = s_active_impl.load(std::memory_order_acquire))
    {
        impl->fatal_signal.store(true, std::memory_order_release);
        (void)impl->stopHookLocked();
    }
}

DQXClarityLauncher::DQXClarityLauncher()
    : pimpl_(std::make_unique<Impl>())
{
    pimpl_->engine = std::make_unique<dqxclarity::Engine>();
    s_active_impl.store(pimpl_.get(), std::memory_order_release);
    utils::CrashHandler::RegisterFatalFlag(&pimpl_->fatal_signal);
    utils::CrashHandler::RegisterFatalCleanup(CrashCleanupThunk);
}

void DQXClarityLauncher::lateInitialize(GlobalStateManager& global_state)
{
    dqxclarity::Config cfg{};
    cfg.enable_post_login_heuristics = true;
    auto& gs = global_state;
    cfg.verbose = gs.verbose();
    cfg.compatibility_mode = gs.compatibilityMode();
    cfg.hook_wait_timeout_ms = gs.hookWaitTimeoutMs();
    
#ifndef _WIN32
    // On Linux/Wine, FORCE compatibility mode regardless of config
    // Wine cannot reliably allocate executable memory for hooks - always use memory scanning
    if (!cfg.compatibility_mode)
    {
        PLOG_WARNING << "Forcing compatibility mode on Linux/Wine (hooks are not supported)";
        cfg.compatibility_mode = true;
        gs.setCompatibilityMode(true); // Update global state so UI reflects this
    }
    
    // Disable proactive verification to avoid false positives from memory cache issues
    cfg.proactive_verify_after_enable_ms = 0;
#endif
    
    pimpl_->engine_cfg = cfg;
    dqxclarity::Logger log{};
    log.info = [](const std::string& m)
    {
#if DQX_PROFILING_LEVEL >= 1
        if (m.find("[PROFILE]") != std::string::npos)
        {
            PLOG_DEBUG_(profiling::kProfilingLogInstance) << m;
        }
        else
#endif
        {
            PLOG_INFO << m;
        }
    };
    log.debug = [](const std::string& m)
    {
        PLOG_DEBUG << m;
    };
    log.warn = [](const std::string& m)
    {
        PLOG_WARNING << m;
    };
    log.error = [this](const std::string& m)
    {
        PLOG_ERROR << m;
        if (pimpl_)
        {
            pimpl_->setLastErrorMessage(m);
        }
    };

    pimpl_->engine->initialize(pimpl_->engine_cfg, std::move(log));
    pimpl_->enable_post_login_heuristics = cfg.enable_post_login_heuristics;

    // Start controller monitor thread
    pimpl_->monitor = std::jthread(
        [this](std::stop_token stoken)
        {
            try
            {
                using namespace std::chrono;
                bool initialized = false;
                while (!stoken.stop_requested())
                {
                    try
                    {
                        pimpl_->heartbeat_seq.fetch_add(1, std::memory_order_relaxed);
                        PLOG_VERBOSE << "Launcher monitor heartbeat " << pimpl_->heartbeat_seq.load();
                        if (!initialized)
                        {
                            pimpl_->process_running_at_start = isDQXGameRunning();
                            initialized = true;
                        }
                        const bool game_running = isDQXGameRunning();
                        auto st = pimpl_->engine->status();
                        if (st == dqxclarity::Status::Hooked)
                        {
                            pimpl_->clearLastErrorMessage();
                            pimpl_->process_warning_reported.store(false, std::memory_order_relaxed);
                        }
                        if (game_running)
                        {
                            if (st == dqxclarity::Status::Stopped || st == dqxclarity::Status::Error)
                            {
                                // If process was already running when tool started, enable immediately once.
                                if (pimpl_->process_running_at_start && !pimpl_->attempted_auto_start)
                                {
                                    if (pimpl_->policy_skip_when_process_running)
                                    {
                                        PLOG_INFO << "Process already running at tool start; enabling immediately";
                                        (void)pimpl_->startHookLocked(
                                            dqxclarity::Engine::StartPolicy::EnableImmediately);
                                    }
                                    else
                                    {
                                        PLOG_INFO << "Process already running but policy defers; waiting for notice screen";
                                        (void)pimpl_->startHookLocked(
                                            dqxclarity::Engine::StartPolicy::DeferUntilIntegrity);
                                    }
                                    pimpl_->attempted_auto_start = true;
                                }
                                else if (!pimpl_->process_running_at_start && !pimpl_->attempted_auto_start)
                                {
                                    // Tool started first, game started later - wait for notice screen internally in engine
                                    PLOG_INFO << "Game process detected; starting hook with DeferUntilIntegrity policy";
                                    (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::DeferUntilIntegrity);
                                    pimpl_->attempted_auto_start = true;
                                }
                            }
                        }
                        else
                        {
                            // Process not running: reset state
                            pimpl_->process_running_at_start = false;
                            pimpl_->attempted_auto_start = false;
                            if (pimpl_->waiting_delay)
                                pimpl_->waiting_delay = false;
                            if (st == dqxclarity::Status::Hooked || st == dqxclarity::Status::Starting ||
                                st == dqxclarity::Status::Stopping)
                            {
                                PLOG_INFO << "DQXGame.exe not running; ensuring hook is stopped";
                                (void)pimpl_->stopHookLocked();
                            }
                            if (st != dqxclarity::Status::Error)
                            {
                                pimpl_->clearLastErrorMessage();
                            }
                        }

                        // Drain new messages from engine and append to backlog
                        std::vector<dqxclarity::DialogMessage> tmp;
                        if (pimpl_->engine->drain(tmp) && !tmp.empty())
                        {
                            std::lock_guard<std::mutex> lock(pimpl_->backlog_mutex);
                            for (auto& m : tmp)
                            {
                                pimpl_->backlog.push_back(std::move(m));
                                if (pimpl_->backlog.size() > pimpl_->kMaxBacklog)
                                {
                                    pimpl_->backlog.erase(pimpl_->backlog.begin(),
                                                          pimpl_->backlog.begin() +
                                                              (pimpl_->backlog.size() - pimpl_->kMaxBacklog));
                                }
                            }
                        }

                        std::vector<dqxclarity::CornerTextItem> corner_text_items;
                        if (pimpl_->engine->drainCornerText(corner_text_items) && !corner_text_items.empty())
                        {
                            std::lock_guard<std::mutex> lock(pimpl_->corner_text_mutex);
                            for (auto& item : corner_text_items)
                            {
                                pimpl_->corner_text_backlog.push_back(std::move(item));
                                if (pimpl_->corner_text_backlog.size() > pimpl_->kMaxCornerTextBacklog)
                                {
                                    pimpl_->corner_text_backlog.erase(
                                        pimpl_->corner_text_backlog.begin(),
                                        pimpl_->corner_text_backlog.begin() +
                                            (pimpl_->corner_text_backlog.size() - pimpl_->kMaxCornerTextBacklog));
                                }
                            }
                        }

                        dqxclarity::QuestMessage quest_snapshot;
                        if (pimpl_->engine->latest_quest(quest_snapshot))
                        {
                            std::lock_guard<std::mutex> qlock(pimpl_->quest_mutex);
                            pimpl_->latest_quest = std::move(quest_snapshot);
                            pimpl_->quest_valid = true;
                        }

                        dqxclarity::PlayerInfo player_snapshot;
                        if (pimpl_->engine->latest_player(player_snapshot))
                        {
                            std::lock_guard<std::mutex> plock(pimpl_->player_mutex);
                            pimpl_->latest_player = std::move(player_snapshot);
                            pimpl_->player_valid = true;
                        }
                        else if (pimpl_->engine->scanPlayerInfo(player_snapshot))
                        {
                            pimpl_->engine->update_player_info(player_snapshot);
                            std::lock_guard<std::mutex> plock(pimpl_->player_mutex);
                            pimpl_->latest_player = std::move(player_snapshot);
                            pimpl_->player_valid = true;
                        }

                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    catch (const std::exception& e)
                    {
                        PLOG_ERROR << "[Monitor] Iteration exception: " << e.what();
                        // Continue running monitor thread despite iteration error
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    catch (...)
                    {
                        PLOG_ERROR << "[Monitor] Unknown iteration exception";
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
            catch (const std::exception& e)
            {
                PLOG_FATAL << "[Monitor] Thread crashed: " << e.what();
                pimpl_->fatal_signal.store(true, std::memory_order_release);
                (void)pimpl_->stopHookLocked();
            }
            catch (...)
            {
                PLOG_FATAL << "[Monitor] Thread crashed with unknown exception";
                pimpl_->fatal_signal.store(true, std::memory_order_release);
                (void)pimpl_->stopHookLocked();
            }
        });

    pimpl_->watchdog = std::jthread(
        [this](std::stop_token stoken)
        {
            using namespace std::chrono;
            std::uint64_t last_seq = pimpl_->heartbeat_seq.load(std::memory_order_relaxed);
            int stagnant_ticks = 0;
            while (!stoken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Reduced from 500ms
                const auto seq = pimpl_->heartbeat_seq.load(std::memory_order_relaxed);
                if (seq == last_seq)
                {
                    if (stagnant_ticks < 6)
                        ++stagnant_ticks;
                }
                else
                {
                    stagnant_ticks = 0;
                    last_seq = seq;
                }
                PLOG_VERBOSE << "Launcher watchdog heartbeat check: seq=" << seq << " stagnant_ticks=" << stagnant_ticks;
                const bool fatal = pimpl_->fatal_signal.load(std::memory_order_acquire);
                const bool in_start_hook = pimpl_->monitor_in_start_hook.load(std::memory_order_acquire);
                
                // Check if engine is in warmup phase (scanning for notice screen)
                auto engine_state = pimpl_->engine->state();
                const bool in_warmup = (engine_state.hook_stage == dqxclarity::HookStage::ScanningForNotice ||
                                       engine_state.hook_stage == dqxclarity::HookStage::WaitingForIntegrity);
                
                // Skip stall checks during start_hook or warmup phase
                const bool skip_stall_check = !fatal && (in_start_hook || in_warmup);
                if (skip_stall_check)
                {
                    stagnant_ticks = 0;
                    last_seq = seq;
                    continue;
                }

                const bool stalled = (stagnant_ticks >= 6);

                auto st = pimpl_->engine->status();
                if ((fatal || stalled) && (st == dqxclarity::Status::Hooked || st == dqxclarity::Status::Starting ||
                                           st == dqxclarity::Status::Stopping))
                {
                    PLOG_FATAL << "Watchdog detected " << (fatal ? "fatal signal" : "heartbeat stall")
                               << "; stopping hook";
                    if (stalled)
                    {
                        PLOG_ERROR << "Watchdog detected heartbeat stall (ticks=" << stagnant_ticks << ")";
                    }
                    (void)pimpl_->stopHookLocked();
                    if (fatal)
                        break;
                    stagnant_ticks = 0;
                    last_seq = seq;
                }
                if (fatal)
                    break;
            }
        });
}

DQXClarityLauncher::~DQXClarityLauncher()
{
    shutdown();
    utils::CrashHandler::RegisterFatalCleanup(nullptr);
    utils::CrashHandler::RegisterFatalFlag(nullptr);
    s_active_impl.store(nullptr, std::memory_order_release);
}

bool DQXClarityLauncher::copyDialogsSince(std::uint64_t since_seq, std::vector<dqxclarity::DialogMessage>& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->backlog_mutex);
    if (pimpl_->backlog.empty())
        return false;
    for (const auto& m : pimpl_->backlog)
    {
        if (m.seq > since_seq)
            out.push_back(m);
    }
    return !out.empty();
}

bool DQXClarityLauncher::copyCornerTextSince(std::uint64_t since_seq,
                                             std::vector<dqxclarity::CornerTextItem>& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->corner_text_mutex);
    if (pimpl_->corner_text_backlog.empty())
        return false;
    for (const auto& item : pimpl_->corner_text_backlog)
    {
        if (item.seq > since_seq)
            out.push_back(item);
    }
    return !out.empty();
}

bool DQXClarityLauncher::getLatestQuest(dqxclarity::QuestMessage& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->quest_mutex);
    if (!pimpl_->quest_valid)
    {
        return false;
    }
    out = pimpl_->latest_quest;
    return true;
}

bool DQXClarityLauncher::getLatestPlayer(dqxclarity::PlayerInfo& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->player_mutex);
    if (!pimpl_->player_valid)
    {
        return false;
    }
    out = pimpl_->latest_player;
    return true;
}

bool DQXClarityLauncher::isDQXGameRunning() const
{
    return dqxclarity::ProcessFinder::IsProcessRunning("DQXGame.exe", false);
}

bool DQXClarityLauncher::launch()
{
    if (!isDQXGameRunning())
    {
        PLOG_WARNING << "Cannot start: DQXGame.exe is not running";
        if (!pimpl_->process_warning_reported.exchange(true, std::memory_order_relaxed))
        {
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::ProcessDetection, "Cannot start Clarity hook",
                                                "DQXGame.exe is not running.");
        }
        return false;
    }
    PLOG_INFO << "Start requested";
    pimpl_->waiting_delay = false;
    bool ok = pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::EnableImmediately);
    if (!ok && pimpl_->getLastErrorMessage().empty())
    {
        pimpl_->setLastErrorMessage("Failed to start hook (EnableImmediately).");
    }
    return ok;
}

bool DQXClarityLauncher::stop()
{
    PLOG_INFO << "Stop requested";
    pimpl_->waiting_delay = false;
    bool ok = pimpl_->stopHookLocked();
    if (!ok && pimpl_->getLastErrorMessage().empty())
    {
        pimpl_->setLastErrorMessage("Failed to stop hook.");
    }
    return ok;
}

bool DQXClarityLauncher::reinitialize(GlobalStateManager& global_state)
{
    PLOG_INFO << "Reinitialize requested - reconfiguring with new compatibility mode";

    // Stop engine hooks but keep monitor thread alive
    if (!stop())
    {
        PLOG_ERROR << "Failed to stop hook during reinitialize";
        return false;
    }

    // Clear all cached data from previous mode
    {
        std::lock_guard<std::mutex> lock(pimpl_->backlog_mutex);
        pimpl_->backlog.clear();
    }
    {
        std::lock_guard<std::mutex> lock(pimpl_->corner_text_mutex);
        pimpl_->corner_text_backlog.clear();
    }
    {
        std::lock_guard<std::mutex> lock(pimpl_->quest_mutex);
        pimpl_->quest_valid = false;
        pimpl_->latest_quest = dqxclarity::QuestMessage{};
    }
    PLOG_INFO << "Cleared cached dialogs from previous mode";

    // Re-read config and re-initialize engine with new settings
    dqxclarity::Config cfg{};
    cfg.enable_post_login_heuristics = true;
    auto& gs = global_state;
    cfg.verbose = gs.verbose();
    cfg.compatibility_mode = gs.compatibilityMode();
    cfg.hook_wait_timeout_ms = gs.hookWaitTimeoutMs();
    
#ifndef _WIN32
    // On Linux/Wine, FORCE compatibility mode regardless of config
    // Wine cannot reliably allocate executable memory for hooks - always use memory scanning
    if (!cfg.compatibility_mode)
    {
        PLOG_WARNING << "Forcing compatibility mode on Linux/Wine (hooks are not supported)";
        cfg.compatibility_mode = true;
        gs.setCompatibilityMode(true); // Update global state so UI reflects this
    }
    
    // Disable proactive verification to avoid false positives from memory cache issues
    cfg.proactive_verify_after_enable_ms = 0;
#endif
    
    pimpl_->engine_cfg = cfg;
    pimpl_->enable_post_login_heuristics = cfg.enable_post_login_heuristics;

    PLOG_INFO << "Compatibility mode setting: "
              << (cfg.compatibility_mode ? "true (memory reader only)" : "false (auto mode)");

    // Re-initialize engine with new config (this updates impl_->cfg)
    dqxclarity::Logger log{};
    log.info = [](const std::string& m)
    {
#if DQX_PROFILING_LEVEL >= 1
        if (m.find("[PROFILE]") != std::string::npos)
        {
            PLOG_DEBUG_(profiling::kProfilingLogInstance) << m;
        }
        else
#endif
        {
            PLOG_INFO << m;
        }
    };
    log.warn = [](const std::string& m)
    {
        PLOG_WARNING << m;
    };
    log.error = [this](const std::string& m)
    {
        PLOG_ERROR << m;
        if (pimpl_)
        {
            pimpl_->setLastErrorMessage(m);
        }
    };

    {
        std::lock_guard<std::mutex> lock(pimpl_->engine_mutex);
        if (!pimpl_->engine->initialize(pimpl_->engine_cfg, std::move(log)))
        {
            PLOG_ERROR << "Failed to re-initialize engine with new config";
            return false;
        }
    }

    // Restart engine if game is running
    if (!isDQXGameRunning())
    {
        PLOG_INFO << "Reinitialize complete (game not running, will auto-start when detected)";
        return true;
    }

    PLOG_INFO << "Game running, restarting with new compatibility mode...";
    return launch();
}

void DQXClarityLauncher::shutdown()
{
    if (!pimpl_)
        return;
    bool expected = false;
    if (!pimpl_->shutdown_called.compare_exchange_strong(expected, true))
    {
        return;
    }

    pimpl_->monitor.request_stop();
    pimpl_->watchdog.request_stop();
    (void)stop();

    if (pimpl_->monitor.joinable())
    {
        pimpl_->monitor.join();
    }

    if (pimpl_->watchdog.joinable())
    {
        pimpl_->watchdog.join();
    }
}

DQXClarityStatus DQXClarityLauncher::getStatus() const
{
    using S = dqxclarity::Status;
    switch (pimpl_->engine->status())
    {
    case S::Stopped:
        return DQXClarityStatus::Stopped;
    case S::Starting:
        return DQXClarityStatus::Running;
    case S::Hooked:
        return DQXClarityStatus::Running;
    case S::Stopping:
        return DQXClarityStatus::Running;
    case S::Error:
        return DQXClarityStatus::Stopped;
    }
    return DQXClarityStatus::Stopped;
}

std::string DQXClarityLauncher::getStatusString() const
{
    using S = dqxclarity::Status;
    auto engine_status = pimpl_->engine->status();
    bool compat_mode = pimpl_->engine_cfg.compatibility_mode;

    switch (engine_status)
    {
    case S::Hooked:
        return compat_mode ? i18n::get("settings.dqxc.status_compatibility_mode") :
                             i18n::get("settings.dqxc.status_auto_mode");
    case S::Starting:
        return i18n::get("settings.dqxc.status_starting");
    case S::Stopping:
        return i18n::get("settings.dqxc.status_stopping");
    case S::Error:
        return i18n::get("settings.dqxc.status_error");
    case S::Stopped:
    default:
        return i18n::get("settings.dqxc.status_error");
    }
}

dqxclarity::Status DQXClarityLauncher::getEngineStage() const { return pimpl_->engine->status(); }

std::string DQXClarityLauncher::getLastErrorMessage() const { return pimpl_->getLastErrorMessage(); }
