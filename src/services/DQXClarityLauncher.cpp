#include "DQXClarityLauncher.hpp"
#include "ProcessDetector.hpp"

#include <plog/Log.h>

#include "dqxclarity/api/dqxclarity.hpp"
#include "dqxclarity/api/dialog_message.hpp"
#include "dqxclarity/api/quest_message.hpp"
#include "dqxclarity/process/NoticeWaiter.hpp"
#include "dqxclarity/process/PostLoginDetector.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

// Private implementation details
struct DQXClarityLauncher::Impl
{
    dqxclarity::Engine engine;
    std::thread monitor;
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> shutdown_called{false};
    std::mutex engine_mutex;
    bool waiting_delay = false;
    std::chrono::steady_clock::time_point detect_tp{};
    int delay_ms = 5000; // default 5s; make configurable later

    // Notice wait worker
    std::thread notice_worker;
    std::atomic<bool> cancel_notice{false};
    std::atomic<bool> notice_found{false};

    // Post-login heuristic detector
    std::thread post_login_worker;
    std::atomic<bool> cancel_post_login{false};
    std::atomic<bool> post_login_found{false};

    // Process state for start policy
    bool process_running_at_start = false;
    bool attempted_auto_start = false;

    mutable std::mutex error_mutex;
    std::string last_error_message;

    // Backlog for UI consumers to read with per-window seq cursors
    mutable std::mutex backlog_mutex;
    std::vector<dqxclarity::DialogMessage> backlog;
    static constexpr std::size_t kMaxBacklog = 2048;

    mutable std::mutex quest_mutex;
    dqxclarity::QuestMessage latest_quest;
    bool quest_valid = false;

    // Config mirrors
    dqxclarity::Config engine_cfg{};
    bool enable_post_login_heuristics = false;
    bool policy_skip_when_process_running = true;
    int notice_wait_timeout_ms = 0; // 0 = infinite

    bool startHookLocked(dqxclarity::Engine::StartPolicy policy)
    {
        std::lock_guard<std::mutex> lock(engine_mutex);
        clearLastErrorMessage();
        return engine.start_hook(policy);
    }

    bool stopHookLocked()
    {
        std::lock_guard<std::mutex> lock(engine_mutex);
        bool ok = engine.stop_hook();
        if (ok) {
            std::lock_guard<std::mutex> qlock(quest_mutex);
            quest_valid = false;
        }
        if (ok) {
            clearLastErrorMessage();
        }
        return ok;
    }

    void setLastErrorMessage(const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error_message = msg;
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

DQXClarityLauncher::DQXClarityLauncher()
    : pimpl_(std::make_unique<Impl>())
{
    dqxclarity::Config cfg{}; // defaults
    // Enable post-login heuristics by default for the utility app
    cfg.enable_post_login_heuristics = true;
    pimpl_->engine_cfg = cfg;
    dqxclarity::Logger log{};
    log.info = [](const std::string& m){ PLOG_INFO << m; };
    log.warn = [](const std::string& m){ PLOG_WARNING << m; };
    log.error = [this](const std::string& m){
        PLOG_ERROR << m;
        if (pimpl_) {
            pimpl_->setLastErrorMessage(m);
        }
    };
    pimpl_->engine.initialize(pimpl_->engine_cfg, std::move(log));
    pimpl_->enable_post_login_heuristics = cfg.enable_post_login_heuristics;

    // Start controller monitor thread
    pimpl_->monitor = std::thread([this]{
        using namespace std::chrono;
        bool initialized = false;
        while (!pimpl_->stop_flag.load()) {
            if (!initialized) {
                pimpl_->process_running_at_start = isDQXGameRunning();
                initialized = true;
            }
            const bool game_running = isDQXGameRunning();
            auto st = pimpl_->engine.status();
            if (st == dqxclarity::Status::Hooked) {
                pimpl_->clearLastErrorMessage();
            }
            if (game_running) {
                if (st == dqxclarity::Status::Stopped || st == dqxclarity::Status::Error) {
                    // If process was already running when tool started, enable immediately once.
                    if (pimpl_->process_running_at_start && !pimpl_->attempted_auto_start) {
                        if (pimpl_->policy_skip_when_process_running) {
                            PLOG_INFO << "Process already running at tool start; enabling immediately";
                            (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::EnableImmediately);
                        } else {
                            // no immediate start; ensure wait workers below will be created
                        }
                        pimpl_->attempted_auto_start = true;
                        // Stop any lingering workers
                        if (pimpl_->notice_worker.joinable()) { pimpl_->cancel_notice.store(true); pimpl_->notice_worker.join(); }
                        if (pimpl_->post_login_worker.joinable()) { pimpl_->cancel_post_login.store(true); pimpl_->post_login_worker.join(); }
                    } else if (!pimpl_->notice_worker.joinable()) {
                        PLOG_INFO << "DQXGame.exe detected; waiting for \"Important notice\" and post-login heuristic";
                        pimpl_->cancel_notice.store(false);
                        pimpl_->notice_found.store(false);
                        pimpl_->notice_worker = std::thread([this]{
                            // Wait for notice with configured timeout (0 = infinite)
                            bool ok = dqxclarity::WaitForNoticeScreen(pimpl_->cancel_notice,
                                std::chrono::milliseconds(250), std::chrono::milliseconds(pimpl_->notice_wait_timeout_ms));
                            if (ok) { pimpl_->notice_found.store(true); }
                        });
                        // Run post-login detector in parallel when enabled
                        if (pimpl_->enable_post_login_heuristics && !pimpl_->post_login_worker.joinable()) {
                            pimpl_->cancel_post_login.store(false);
                            pimpl_->post_login_found.store(false);
                            pimpl_->post_login_worker = std::thread([this]{
                                bool ok = dqxclarity::DetectPostLogin(pimpl_->cancel_post_login,
                                    std::chrono::milliseconds(250), std::chrono::milliseconds(0));
                                if (ok) { pimpl_->post_login_found.store(true); }
                            });
                        }
                    }

                    // If either signal is observed, start accordingly
                    if (pimpl_->notice_found.load()) {
                        PLOG_INFO << "Important notice found; starting hook (defer until integrity)...";
                        (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::DeferUntilIntegrity);
                        pimpl_->notice_found.store(false);
                        // Cancel heuristic worker
                        if (pimpl_->post_login_worker.joinable()) { pimpl_->cancel_post_login.store(true); pimpl_->post_login_worker.join(); }
                        if (pimpl_->notice_worker.joinable()) { pimpl_->notice_worker.join(); }
                    } else if (pimpl_->post_login_found.load()) {
                        PLOG_INFO << "Post-login heuristic matched; enabling immediately...";
                        (void)pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::EnableImmediately);
                        pimpl_->post_login_found.store(false);
                        // Cancel notice worker
                        if (pimpl_->notice_worker.joinable()) { pimpl_->cancel_notice.store(true); pimpl_->notice_worker.join(); }
                        if (pimpl_->post_login_worker.joinable()) { pimpl_->post_login_worker.join(); }
                    }
                }
            } else {
                // Process not running: reset state and cancel any pending workers
                pimpl_->process_running_at_start = false;
                pimpl_->attempted_auto_start = false;
                if (pimpl_->notice_worker.joinable()) {
                    pimpl_->cancel_notice.store(true);
                    pimpl_->notice_worker.join();
                }
                if (pimpl_->post_login_worker.joinable()) {
                    pimpl_->cancel_post_login.store(true);
                    pimpl_->post_login_worker.join();
                }
                if (pimpl_->waiting_delay) pimpl_->waiting_delay = false;
                if (st == dqxclarity::Status::Hooked || st == dqxclarity::Status::Starting || st == dqxclarity::Status::Stopping) {
                    PLOG_INFO << "DQXGame.exe not running; ensuring hook is stopped";
                    (void)pimpl_->stopHookLocked();
                }
                if (st != dqxclarity::Status::Error) {
                    pimpl_->clearLastErrorMessage();
                }
            }

            // Drain new messages from engine and append to backlog
            std::vector<dqxclarity::DialogMessage> tmp;
            if (pimpl_->engine.drain(tmp) && !tmp.empty()) {
                std::lock_guard<std::mutex> lock(pimpl_->backlog_mutex);
                for (auto& m : tmp) {
                    pimpl_->backlog.push_back(std::move(m));
                    if (pimpl_->backlog.size() > pimpl_->kMaxBacklog) {
                        pimpl_->backlog.erase(pimpl_->backlog.begin(), pimpl_->backlog.begin() + (pimpl_->backlog.size() - pimpl_->kMaxBacklog));
                    }
                }
            }

            dqxclarity::QuestMessage quest_snapshot;
            if (pimpl_->engine.latest_quest(quest_snapshot)) {
                std::lock_guard<std::mutex> qlock(pimpl_->quest_mutex);
                pimpl_->latest_quest = std::move(quest_snapshot);
                pimpl_->quest_valid = true;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

DQXClarityLauncher::~DQXClarityLauncher()
{
    shutdown();
}

bool DQXClarityLauncher::copyDialogsSince(std::uint64_t since_seq, std::vector<dqxclarity::DialogMessage>& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->backlog_mutex);
    if (pimpl_->backlog.empty()) return false;
    for (const auto& m : pimpl_->backlog) {
        if (m.seq > since_seq) out.push_back(m);
    }
    return !out.empty();
}

bool DQXClarityLauncher::getLatestQuest(dqxclarity::QuestMessage& out) const
{
    std::lock_guard<std::mutex> lock(pimpl_->quest_mutex);
    if (!pimpl_->quest_valid) {
        return false;
    }
    out = pimpl_->latest_quest;
    return true;
}

bool DQXClarityLauncher::isDQXGameRunning() const
{
    return ProcessDetector::isProcessRunning("DQXGame.exe");
}

bool DQXClarityLauncher::launch()
{
    if (!isDQXGameRunning())
    {
        PLOG_WARNING << "Cannot start: DQXGame.exe is not running";
        return false;
    }
    PLOG_INFO << "Start requested";
    pimpl_->waiting_delay = false; // cancel any pending delay and start now
    if (pimpl_->notice_worker.joinable()) {
        pimpl_->cancel_notice.store(true);
        pimpl_->notice_worker.join();
    }
    if (pimpl_->post_login_worker.joinable()) {
        pimpl_->cancel_post_login.store(true);
        pimpl_->post_login_worker.join();
    }
    return pimpl_->startHookLocked(dqxclarity::Engine::StartPolicy::EnableImmediately);
}

bool DQXClarityLauncher::stop()
{
    PLOG_INFO << "Stop requested";
    pimpl_->waiting_delay = false;
    pimpl_->cancel_notice.store(true);
    if (pimpl_->notice_worker.joinable()) {
        pimpl_->notice_worker.join();
    }
    pimpl_->cancel_post_login.store(true);
    if (pimpl_->post_login_worker.joinable()) {
        pimpl_->post_login_worker.join();
    }
    return pimpl_->stopHookLocked();
}

void DQXClarityLauncher::shutdown()
{
    if (!pimpl_) return;
    bool expected = false;
    if (!pimpl_->shutdown_called.compare_exchange_strong(expected, true)) {
        return;
    }

    pimpl_->stop_flag.store(true);
    (void)stop();

    if (pimpl_->monitor.joinable()) {
        pimpl_->monitor.join();
    }

    if (pimpl_->notice_worker.joinable()) {
        pimpl_->cancel_notice.store(true);
        pimpl_->notice_worker.join();
    }

    if (pimpl_->post_login_worker.joinable()) {
        pimpl_->cancel_post_login.store(true);
        pimpl_->post_login_worker.join();
    }
}

DQXClarityStatus DQXClarityLauncher::getStatus() const
{
    using S = dqxclarity::Status;
    switch (pimpl_->engine.status())
    {
        case S::Stopped: return DQXClarityStatus::Stopped;
        case S::Starting: return DQXClarityStatus::Running;
        case S::Hooked: return DQXClarityStatus::Running;
        case S::Stopping: return DQXClarityStatus::Running;
        case S::Error: return DQXClarityStatus::Stopped;
    }
    return DQXClarityStatus::Stopped;
}

std::string DQXClarityLauncher::getStatusString() const
{
    switch (getStatus())
    {
        case DQXClarityStatus::Stopped: return "Stopped";
        case DQXClarityStatus::Running: return "Running";
        case DQXClarityStatus::Connected: return "OK";
        case DQXClarityStatus::Disconnected: return "Disconnected";
        default: return "Unknown";
    }
}

dqxclarity::Status DQXClarityLauncher::getEngineStage() const
{
    return pimpl_->engine.status();
}

std::string DQXClarityLauncher::getLastErrorMessage() const
{
    return pimpl_->getLastErrorMessage();
}


