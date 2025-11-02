#include "DQXClarityLauncher.hpp"
#include "ProcessDetector.hpp"

#include <plog/Log.h>

#include "../config/ConfigManager.hpp"
#include "../ui/Localization.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../utils/CrashHandler.hpp"
#include "../utils/Profile.hpp"

#include "dqxclarity/api/dqxclarity.hpp"
#include "dqxclarity/api/dialog_message.hpp"
#include "dqxclarity/api/dialog_stream.hpp"
#include "dqxclarity/api/player_info.hpp"
#include "dqxclarity/api/quest_message.hpp"
#include "DQXClarityService.hpp"
#include "dqxclarity/process/ProcessFinder.hpp"
#include "dqxclarity/memory/MemoryFactory.hpp"
#include "dqxclarity/memory/IProcessMemory.hpp"
#include "dqxclarity/pattern/IMemoryScanner.hpp"
#include "dqxclarity/pattern/polling/PollingRunner.hpp"
#include "dqxclarity/pattern/polling/PatternPollingTask.hpp"
#include "dqxclarity/signatures/Signatures.hpp"
#include "dqxclarity/hooking/HookRegistry.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <exception>
#include <optional>

namespace
{

bool run_pattern_waiter(const char* name, const dqxclarity::Pattern& pattern, bool require_executable,
                        std::chrono::milliseconds poll_interval, std::chrono::milliseconds timeout,
                        std::atomic<bool>& cancel)
{
    auto pids = dqxclarity::ProcessFinder::FindByName("DQXGame.exe", false);
    if (pids.empty())
    {
        return false;
    }

    auto memory_unique = dqxclarity::MemoryFactory::CreatePlatformMemory();
    std::shared_ptr<dqxclarity::IProcessMemory> memory(std::move(memory_unique));
    if (!memory || !memory->AttachProcess(pids[0]))
    {
        return false;
    }

    auto scanner = std::make_shared<dqxclarity::ProcessMemoryScanner>(memory);
    dqxclarity::PollingRunner runner(scanner);
    bool matched = false;

    std::optional<std::chrono::milliseconds> timeout_opt;
    if (timeout.count() > 0)
    {
        timeout_opt = timeout;
    }

    dqxclarity::PatternPollingTask task(name, pattern, require_executable, poll_interval, timeout_opt,
                                        dqxclarity::TerminationMode::FirstMatch,
                                        [&](uintptr_t)
                                        {
                                            matched = true;
                                        });

    auto result = runner.Run(task, cancel);
    memory->DetachProcess();
    return matched && result.status == dqxclarity::PollingResult::Status::Matched;
}

bool wait_for_notice_screen(std::atomic<bool>& cancel, std::chrono::milliseconds poll_interval,
                            std::chrono::milliseconds timeout)
{
    return run_pattern_waiter("NoticeWaiter", dqxclarity::Signatures::GetNoticeString(), /*require_executable=*/false,
                              poll_interval, timeout, cancel);
}

bool detect_post_login(std::atomic<bool>& cancel, std::chrono::milliseconds poll_interval,
                       std::chrono::milliseconds timeout)
{
    return run_pattern_waiter("PostLoginDetector", dqxclarity::Signatures::GetWalkthroughPattern(),
                              /*require_executable=*/false, poll_interval, timeout, cancel);
}

bool scan_player_names(dqxclarity::PlayerInfo& out)
{
    using namespace dqxclarity;
    PLOG_DEBUG << "[PlayerScan] begin polling";

    auto pids = ProcessFinder::FindByName("DQXGame.exe", false);
    if (pids.empty())
    {
        PLOG_DEBUG << "[PlayerScan] DQXGame.exe not found";
        return false;
    }

    auto memory_unique = MemoryFactory::CreatePlatformMemory();
    std::shared_ptr<IProcessMemory> memory(std::move(memory_unique));
    if (!memory || !memory->AttachProcess(pids[0]))
    {
        PLOG_DEBUG << "[PlayerScan] failed to attach to process";
        return false;
    }

    auto scanner = std::make_shared<ProcessMemoryScanner>(memory);
    PollingRunner runner(scanner);
    std::atomic<bool> cancel{ false };
    PlayerInfo captured;
    bool captured_valid = false;

    auto callback = [&](uintptr_t address)
    {
        std::string player_name;
        std::string sibling_name;
        if (!memory->ReadString(address - 21, player_name, 128))
            return;
        if (!memory->ReadString(address + 51, sibling_name, 128))
            return;
        if (!player_name.empty() && static_cast<unsigned char>(player_name.front()) < 0x20)
            player_name.erase(player_name.begin());
        if (!sibling_name.empty() && static_cast<unsigned char>(sibling_name.front()) < 0x20)
            sibling_name.erase(sibling_name.begin());
        if (player_name.empty() || sibling_name.empty())
            return;
        captured.player_name = std::move(player_name);
        captured.sibling_name = std::move(sibling_name);
        captured.relationship = PlayerRelationship::Unknown;
        captured_valid = true;
    };

    PatternPollingTask task("PlayerNamePatternScan", Signatures::GetSiblingNamePattern(), false,
                            std::chrono::milliseconds(250), std::chrono::milliseconds(2000),
                            TerminationMode::FirstMatch, callback);

    auto result = runner.Run(task, cancel);
    memory->DetachProcess();

    if (captured_valid && result.status == PollingResult::Status::Matched)
    {
        PLOG_INFO << "[PlayerScan] captured player=\"" << captured.player_name << "\" sibling=\""
                  << captured.sibling_name << "\"";
        out = std::move(captured);
        return true;
    }
    PLOG_DEBUG << "[PlayerScan] no match (status=" << static_cast<int>(result.status) << ")";
    return false;
}

} // namespace

namespace dqxclarity
{

} // namespace dqxclarity

// Private implementation details
struct DQXClarityLauncher::Impl
{
    std::unique_ptr<dqxclarity::Engine> engine;
    std::thread monitor;
    std::atomic<bool> stop_flag{ false };
    std::atomic<bool> shutdown_called{ false };
    std::mutex engine_mutex;
    bool waiting_delay = false;
    std::chrono::steady_clock::time_point detect_tp{};
    int delay_ms = 5000; // default 5s; make configurable later

    std::thread watchdog;
    std::atomic<bool> watchdog_stop{ false };
    std::atomic<std::uint64_t> heartbeat_seq{ 0 };
    std::atomic<bool> fatal_signal{ false };
    std::atomic<bool> stop_in_progress{ false };

    // Notice wait worker
    std::thread notice_worker;
    std::atomic<bool> cancel_notice{ false };
    std::atomic<bool> notice_found{ false };

    // Post-login heuristic detector
    std::thread post_login_worker;
    std::atomic<bool> cancel_post_login{ false };
    std::atomic<bool> post_login_found{ false };

    // Process state for start policy
    bool process_running_at_start = false;
    bool attempted_auto_start = false;

    mutable std::mutex error_mutex;
    std::string last_error_message;
    std::atomic<bool> process_warning_reported{ false };

    // Backlog for UI consumers to read with per-window seq cursors
    mutable std::mutex backlog_mutex;
    std::vector<dqxclarity::DialogMessage> backlog;
    static constexpr std::size_t kMaxBacklog = 2048;

    mutable std::mutex stream_mutex;
    std::vector<dqxclarity::DialogStreamItem> stream_backlog;
    static constexpr std::size_t kMaxStreamBacklog = 2048;

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
        std::lock_guard<std::mutex> lock(engine_mutex);
        clearLastErrorMessage();
        bool ok = engine->start_hook(policy);
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

void DQXClarityLauncher::lateInitialize(ConfigManager& config)
{
    dqxclarity::Config cfg{};
    cfg.enable_post_login_heuristics = true;
    auto& gs = config.globalState();
    cfg.verbose = gs.verbose();
    cfg.compatibility_mode = gs.compatibilityMode();
    cfg.hook_wait_timeout_ms = gs.hookWaitTimeoutMs();
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

    dqxclarity::persistence::HookRegistry::SetLogger(log);
    dqxclarity::persistence::HookRegistry::CheckAndCleanup();

    pimpl_->engine->initialize(pimpl_->engine_cfg, std::move(log));
    pimpl_->enable_post_login_heuristics = cfg.enable_post_login_heuristics;

    // Start controller monitor thread
    pimpl_->monitor = std::thread(
        [this]
        {
            try
            {
                using namespace std::chrono;
                bool initialized = false;
                while (!pimpl_->stop_flag.load())
                {
                    try
                    {
                        pimpl_->heartbeat_seq.fetch_add(1, std::memory_order_relaxed);
                        PLOG_DEBUG << "Launcher monitor heartbeat " << pimpl_->heartbeat_seq.load();
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
                                        // no immediate start; ensure wait workers below will be created
                                    }
                                    pimpl_->attempted_auto_start = true;
                                    // Stop any lingering workers
                                    if (pimpl_->notice_worker.joinable())
                                    {
                                        pimpl_->cancel_notice.store(true);
                                        pimpl_->notice_worker.join();
                                    }
                                    if (pimpl_->post_login_worker.joinable())
                                    {
                                        pimpl_->cancel_post_login.store(true);
                                        pimpl_->post_login_worker.join();
                                    }
                                }
                                else if (!pimpl_->notice_worker.joinable())
                                {
                                    PLOG_INFO << "DQXGame.exe detected; waiting for \"Important notice\" and "
                                                 "post-login heuristic";
                                    pimpl_->cancel_notice.store(false);
                                    pimpl_->notice_found.store(false);
                                    pimpl_->notice_worker = std::thread(
                                        [this]
                                        {
                                            // Wait for notice with configured timeout (0 = infinite)
                                            bool ok = wait_for_notice_screen(
                                                pimpl_->cancel_notice, std::chrono::milliseconds(250),
                                                std::chrono::milliseconds(pimpl_->notice_wait_timeout_ms));
                                            if (ok)
                                            {
                                                pimpl_->notice_found.store(true);
                                            }
                                        });
                                    // Run post-login detector in parallel when enabled
                                    if (pimpl_->enable_post_login_heuristics && !pimpl_->post_login_worker.joinable())
                                    {
                                        pimpl_->cancel_post_login.store(false);
                                        pimpl_->post_login_found.store(false);
                                        pimpl_->post_login_worker = std::thread(
                                            [this]
                                            {
                                                bool ok = detect_post_login(pimpl_->cancel_post_login,
                                                                            std::chrono::milliseconds(250),
                                                                            std::chrono::milliseconds(0));
                                                if (ok)
                                                {
                                                    pimpl_->post_login_found.store(true);
                                                }
                                            });
                                    }
                                }

                                // If either signal is observed, start accordingly
                                if (pimpl_->notice_found.load())
                                {
                                    PLOG_INFO << "Important notice found; starting hook (defer until integrity)...";
                                    (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::DeferUntilIntegrity);
                                    pimpl_->notice_found.store(false);
                                    // Cancel heuristic worker
                                    if (pimpl_->post_login_worker.joinable())
                                    {
                                        pimpl_->cancel_post_login.store(true);
                                        pimpl_->post_login_worker.join();
                                    }
                                    if (pimpl_->notice_worker.joinable())
                                    {
                                        pimpl_->notice_worker.join();
                                    }
                                }
                                else if (pimpl_->post_login_found.load())
                                {
                                    PLOG_INFO << "Post-login heuristic matched; enabling immediately...";
                                    (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::EnableImmediately);
                                    pimpl_->post_login_found.store(false);
                                    // Cancel notice worker
                                    if (pimpl_->notice_worker.joinable())
                                    {
                                        pimpl_->cancel_notice.store(true);
                                        pimpl_->notice_worker.join();
                                    }
                                    if (pimpl_->post_login_worker.joinable())
                                    {
                                        pimpl_->post_login_worker.join();
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Process not running: reset state and cancel any pending workers
                            pimpl_->process_running_at_start = false;
                            pimpl_->attempted_auto_start = false;
                            if (pimpl_->notice_worker.joinable())
                            {
                                pimpl_->cancel_notice.store(true);
                                pimpl_->notice_worker.join();
                            }
                            if (pimpl_->post_login_worker.joinable())
                            {
                                pimpl_->cancel_post_login.store(true);
                                pimpl_->post_login_worker.join();
                            }
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

                        std::vector<dqxclarity::DialogStreamItem> stream_items;
                        if (pimpl_->engine->drainStream(stream_items) && !stream_items.empty())
                        {
                            std::lock_guard<std::mutex> lock(pimpl_->stream_mutex);
                            for (auto& item : stream_items)
                            {
                                pimpl_->stream_backlog.push_back(std::move(item));
                                if (pimpl_->stream_backlog.size() > pimpl_->kMaxStreamBacklog)
                                {
                                    pimpl_->stream_backlog.erase(
                                        pimpl_->stream_backlog.begin(),
                                        pimpl_->stream_backlog.begin() +
                                            (pimpl_->stream_backlog.size() - pimpl_->kMaxStreamBacklog));
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

#if 0
                dqxclarity::PlayerInfo player_snapshot;
                if (pimpl_->engine->latest_player(player_snapshot))
                {
                    std::lock_guard<std::mutex> plock(pimpl_->player_mutex);
                    pimpl_->latest_player = std::move(player_snapshot);
                    pimpl_->player_valid = true;
                }
                else if (scan_player_names(player_snapshot))
                {
                    pimpl_->engine->update_player_info(player_snapshot);
                    std::lock_guard<std::mutex> plock(pimpl_->player_mutex);
                    pimpl_->latest_player = std::move(player_snapshot);
                    pimpl_->player_valid = true;
                }
#endif

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

    pimpl_->watchdog = std::thread(
        [this]
        {
            using namespace std::chrono;
            std::uint64_t last_seq = pimpl_->heartbeat_seq.load(std::memory_order_relaxed);
            int stagnant_ticks = 0;
            while (!pimpl_->watchdog_stop.load(std::memory_order_acquire))
            {
                if (pimpl_->stop_flag.load(std::memory_order_acquire))
                    break;
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
                PLOG_DEBUG << "Launcher watchdog heartbeat check: seq=" << seq << " stagnant_ticks=" << stagnant_ticks;
                const bool fatal = pimpl_->fatal_signal.load(std::memory_order_acquire);
                const bool stalled = stagnant_ticks >= 6; // Reduced from 6

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

bool DQXClarityLauncher::copyDialogStreamSince(std::uint64_t since_seq,
                                               std::vector<dqxclarity::DialogStreamItem>& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->stream_mutex);
    if (pimpl_->stream_backlog.empty())
        return false;
    for (const auto& item : pimpl_->stream_backlog)
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

bool DQXClarityLauncher::isDQXGameRunning() const { return ProcessDetector::isProcessRunning("DQXGame.exe"); }

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
    pimpl_->waiting_delay = false; // cancel any pending delay and start now
    if (pimpl_->notice_worker.joinable())
    {
        pimpl_->cancel_notice.store(true);
        pimpl_->notice_worker.join();
    }
    if (pimpl_->post_login_worker.joinable())
    {
        pimpl_->cancel_post_login.store(true);
        pimpl_->post_login_worker.join();
    }
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
    pimpl_->cancel_notice.store(true);
    if (pimpl_->notice_worker.joinable())
    {
        pimpl_->notice_worker.join();
    }
    pimpl_->cancel_post_login.store(true);
    if (pimpl_->post_login_worker.joinable())
    {
        pimpl_->post_login_worker.join();
    }
    bool ok = pimpl_->stopHookLocked();
    if (!ok && pimpl_->getLastErrorMessage().empty())
    {
        pimpl_->setLastErrorMessage("Failed to stop hook.");
    }
    return ok;
}

bool DQXClarityLauncher::reinitialize(ConfigManager& config)
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
        std::lock_guard<std::mutex> lock(pimpl_->stream_mutex);
        pimpl_->stream_backlog.clear();
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
    auto& gs = config.globalState();
    cfg.verbose = gs.verbose();
    cfg.compatibility_mode = gs.compatibilityMode();
    cfg.hook_wait_timeout_ms = gs.hookWaitTimeoutMs();
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

    pimpl_->stop_flag.store(true);
    pimpl_->watchdog_stop.store(true, std::memory_order_release);
    (void)stop();

    if (pimpl_->monitor.joinable())
    {
        pimpl_->monitor.join();
    }

    if (pimpl_->watchdog.joinable())
    {
        pimpl_->watchdog.join();
    }

    if (pimpl_->notice_worker.joinable())
    {
        pimpl_->cancel_notice.store(true);
        pimpl_->notice_worker.join();
    }

    if (pimpl_->post_login_worker.joinable())
    {
        pimpl_->cancel_post_login.store(true);
        pimpl_->post_login_worker.join();
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
