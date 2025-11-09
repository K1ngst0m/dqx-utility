#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../process/ProcessFinder.hpp"
#include "../hooking/DialogHook.hpp"
#include "../hooking/CornerTextHook.hpp"
#include "../hooking/NetworkTextHook.hpp"
#include "../hooking/PlayerHook.hpp"
#include "../hooking/QuestHook.hpp"
#include "../hooking/HookCreateInfo.hpp"
#include "../hooking/HookManager.hpp"
#include "../hooking/IntegrityHook.hpp"
#include "../hooking/IntegrityMonitor.hpp"
#include "../hooking/HookRegistry.hpp"
#include "../scanning/ScannerManager.hpp"
#include "../scanning/DialogScanner.hpp"
#include "../scanning/QuestScanner.hpp"
#include "../scanning/NoticeScreenScanner.hpp"
#include "../scanning/PostLoginScanner.hpp"
#include "../scanning/PlayerNameScanner.hpp"
#include "../scanning/ScannerCreateInfo.hpp"
#include "../signatures/Signatures.hpp"
#include "../pattern/Pattern.hpp"
#include "dialog_message.hpp"
#include "quest_message.hpp"
#include "corner_text.hpp"
#include "../util/SPSCRing.hpp"
#include "../util/Profile.hpp"
#include "../pattern/MemoryRegion.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <future>

#ifdef _WIN32
#undef min
#undef max
#endif
#include "../util/BS_thread_pool.hpp"

namespace dqxclarity
{

struct PendingDialog
{
    std::string text;
    std::string speaker;
    std::chrono::steady_clock::time_point capture_time;

    enum Source
    {
        Hook,
        Scanner
    } source;
};

struct PendingQuest
{
    std::string key;
    std::string subquest_name;
    std::string quest_name;
    std::string description;
    std::string rewards;
    std::string repeat_rewards;
    std::chrono::steady_clock::time_point capture_time;

    enum Source
    {
        Hook,
        Scanner
    } source;
};

struct Engine::Impl
{
    Config cfg{};
    Logger log{};
    std::unique_ptr<IProcessMemory> memory = nullptr;
    
    // Centralized hook lifecycle manager
    HookManager hook_manager;
    
    // Centralized scanner lifecycle manager
    std::unique_ptr<ScannerManager> scanner_manager;

    SpscRing<DialogMessage, 1024> ring;
    std::atomic<std::uint64_t> seq{ 0 };
    SpscRing<CornerTextItem, 512> corner_text_ring;
    std::atomic<std::uint64_t> corner_text_seq{ 0 };
    std::jthread poller;
    std::unique_ptr<IntegrityMonitor> monitor;

    std::atomic<std::uint64_t> quest_seq{ 0 };
    mutable std::mutex quest_mutex;
    QuestMessage quest_snapshot;
    bool quest_valid = false;

    std::atomic<std::uint64_t> player_seq{ 0 };
    mutable std::mutex player_mutex;
    PlayerInfo player_snapshot;
    bool player_valid = false;

    // Progress tracking
    std::atomic<HookStage> hook_stage{ HookStage::Idle };
    mutable std::mutex error_mutex;
    std::string last_error_message;

    // Scanner state tracking
    std::atomic<bool> notice_screen_visible{ false };
    std::atomic<bool> post_login_detected{ false };

    std::atomic<std::uint64_t> notice_listener_seq{ 1 };
    std::mutex notice_listener_mutex;
    std::unordered_map<std::uint64_t, std::function<void(bool)>> notice_listeners;

    std::atomic<std::uint64_t> post_login_listener_seq{ 1 };
    std::mutex post_login_listener_mutex;
    std::unordered_map<std::uint64_t, std::function<void(bool)>> post_login_listeners;
    
    // Scanner warmup for notice/post-login detection before hooks
    std::jthread warmup_thread;
    std::atomic<bool> warmup_shutdown{ false };
    std::condition_variable warmup_cv;
    std::mutex warmup_mutex;
    std::jthread delayed_enable_thread;
    
    // Thread pool for parallel scanner polling
    std::unique_ptr<BS::light_thread_pool> scanner_pool;

    // Two-phase commit: pending queue for deduplication
    std::vector<PendingDialog> pending_dialogs;
    std::mutex pending_mutex;

    // Global deduplication cache: prevent duplicates across batches
    struct PublishedDialog
    {
        std::string text;
        std::chrono::steady_clock::time_point publish_time;
    };

    std::vector<PublishedDialog> published_cache;
    std::mutex cache_mutex;
    static constexpr std::chrono::milliseconds CACHE_EXPIRY_MS{ 5000 }; // 5 seconds

    // Quest pending queue and cache (hook priority, scanner fallback)
    std::vector<PendingQuest> pending_quests;
    std::mutex pending_quest_mutex;
    struct PublishedQuest
    {
        std::string key;
        std::chrono::steady_clock::time_point publish_time;
    };
    std::vector<PublishedQuest> quest_published_cache;
    std::mutex quest_cache_mutex;
    static constexpr std::chrono::milliseconds QUEST_CACHE_EXPIRY_MS{ 5000 };

    // Diagnostics: latency tracking for both capture methods
    struct CaptureTimings
    {
        std::chrono::steady_clock::time_point hook_captured;
        std::chrono::steady_clock::time_point scanner_captured;
        bool hook_valid = false;
        bool scanner_valid = false;
    } last_capture_timings;

    std::mutex diagnostics_mutex;

    // Helper methods for scanner warmup phase
    void SetError(const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error_message = msg;
        if (log.error)
            log.error(msg);
    }

    /**
     * @brief Wait for scanner readiness based on policy
     * 
     * For DeferUntilIntegrity: waits for notice screen detection
     * For EnableImmediately: returns immediately
     * 
     * @param policy The startup policy
     * @param timeout Maximum time to wait (0 = infinite)
     * @return true if conditions satisfied, false on timeout or error
     */
    bool WaitForScannerReadiness(Engine::StartPolicy policy, std::chrono::milliseconds timeout)
    {
        if (policy == Engine::StartPolicy::EnableImmediately)
        {
            if (log.info)
                log.info("StartPolicy::EnableImmediately - skipping scanner warmup");
            return true;
        }

        if (log.debug)
            log.debug("[warmup] Waiting for notice screen pattern before enabling hooks");

        hook_stage.store(HookStage::ScanningForNotice, std::memory_order_release);

        auto start_time = std::chrono::steady_clock::now();
        const auto poll_interval = std::chrono::milliseconds(100);
        int poll_counter = 0;

        while (!warmup_shutdown.load(std::memory_order_acquire))
        {
            // Check if notice screen is visible
            if (notice_screen_visible.load(std::memory_order_acquire))
            {
                if (log.debug)
                    log.debug("[warmup] Notice screen detected - proceeding to hook installation");
                hook_stage.store(HookStage::WaitingForIntegrity, std::memory_order_release);
                return true;
            }

            // Check timeout (if specified)
            if (timeout.count() > 0)
            {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed >= timeout)
                {
                    SetError("Timeout waiting for notice screen pattern");
                    return false;
                }
            }

            // Sleep and continue polling
            std::this_thread::sleep_for(poll_interval);

            if (++poll_counter % 10 == 0 && cfg.verbose && log.info)
            {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time)
                                      .count();
                log.info("[warmup] Still waiting for notice screen (" + std::to_string(elapsed_ms) + " ms elapsed)");
            }
        }

        // Shutdown requested
        return false;
    }
};

Engine::Engine()
    : impl_(new Impl{})
{
}

Engine::~Engine() noexcept { stop_hook(); }

bool Engine::initialize(const Config& cfg, Logger loggers)
{
    impl_->cfg = cfg;
    impl_->log = std::move(loggers);
    status_ = Status::Stopped;

#if DQX_PROFILING_LEVEL >= 1
    // Set profiling logger to route profiling output through dqxclarity's Logger
    profiling::SetProfilingLogger(&impl_->log);
#endif

    // Initialize hook persistence system
    persistence::HookRegistry::SetLogger(impl_->log);
    persistence::HookRegistry::CheckAndCleanup();

    return true;
}

bool Engine::start_hook()
{
    return start_hook(StartPolicy{ impl_->cfg.defer_dialog_patch ? StartPolicy::DeferUntilIntegrity :
                                                                   StartPolicy::EnableImmediately });
}

bool Engine::start_hook(StartPolicy policy)
{
    PROFILE_SCOPE_FUNCTION();
    if (status_ == Status::Hooked || status_ == Status::Starting)
        return true;
    status_ = Status::Starting;

    // Reset state
    impl_->warmup_shutdown.store(false, std::memory_order_release);
    impl_->hook_stage.store(HookStage::AttachingProcess, std::memory_order_release);
    impl_->quest_seq.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(impl_->quest_mutex);
        impl_->quest_valid = false;
        impl_->quest_snapshot = QuestMessage{};
    }
    impl_->player_seq.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(impl_->player_mutex);
        impl_->player_valid = false;
        impl_->player_snapshot = PlayerInfo{};
    }

    // PHASE 1: Attach to DQXGame.exe
    {
        PROFILE_SCOPE_CUSTOM("Engine.FindProcess");
        auto pids = dqxclarity::ProcessFinder::FindByName("DQXGame.exe", false);
        if (pids.empty())
        {
            impl_->SetError("DQXGame.exe not found");
            status_ = Status::Error;
            impl_->hook_stage.store(HookStage::Idle, std::memory_order_release);
            return false;
        }

        // Create memory interface and attach
        impl_->memory = dqxclarity::MemoryFactory::CreatePlatformMemory();
        if (!impl_->memory || !impl_->memory->AttachProcess(pids[0]))
        {
            impl_->SetError("Failed to attach to DQXGame.exe");
            status_ = Status::Error;
            impl_->hook_stage.store(HookStage::Idle, std::memory_order_release);
            return false;
        }

        if (impl_->log.info)
            impl_->log.info("Attached to DQXGame.exe successfully");
    }

    // Initialize thread pool for parallel scanner polling (4 workers)
    impl_->scanner_pool = std::make_unique<BS::light_thread_pool>(4);

    // Parse memory regions once to avoid repeated parsing (optimization)
    std::vector<MemoryRegion> cached_regions;
    {
        PROFILE_SCOPE_CUSTOM("Engine.ParseMemoryRegions");
        cached_regions = MemoryRegionParser::ParseMaps(impl_->memory->GetAttachedPid());
    }

    // Build common HookCreateInfo for all hooks
    HookCreateInfo base_hook_info;
    base_hook_info.memory = impl_->memory.get();
    base_hook_info.logger = impl_->log;
    base_hook_info.verbose = impl_->cfg.verbose;
    base_hook_info.instruction_safe_steal = impl_->cfg.instruction_safe_steal;
    base_hook_info.readback_bytes = static_cast<size_t>(impl_->cfg.readback_bytes);
    base_hook_info.cached_regions = cached_regions;

    // Initialize ScannerManager
    impl_->scanner_manager = std::make_unique<ScannerManager>();

    // Initialize dialog capture state (hook deferred until after warmup)
    bool dialog_hook_installed = false;

    // Mode 1: Compatibility mode (memory reader only, safer)
    if (impl_->cfg.compatibility_mode)
    {
        if (impl_->log.info)
            impl_->log.info("Compatibility mode: using dialog scanner only (no hooking)");

        PROFILE_SCOPE_CUSTOM("Engine.InitializeDialogScanner");
        ScannerCreateInfo dialog_info;
        dialog_info.memory = impl_->memory.get();
        dialog_info.logger = impl_->log;
        dialog_info.verbose = impl_->cfg.verbose;
        dialog_info.pattern = Signatures::GetDialogPattern();
        
        auto dialog_scanner = std::make_unique<DialogScanner>(dialog_info);
        if (!dialog_scanner->Initialize())
        {
            if (impl_->log.error)
                impl_->log.error("Failed to initialize dialog scanner in compatibility mode");
            status_ = Status::Error;
            return false;
        }
        if (!impl_->scanner_manager->RegisterScanner(ScannerType::Dialog, std::move(dialog_scanner)))
        {
            if (impl_->log.error)
                impl_->log.error("Failed to register dialog scanner in compatibility mode");
            status_ = Status::Error;
            return false;
        }
        if (impl_->log.info)
            impl_->log.info("Dialog scanner initialized successfully (compatibility mode)");

        // Quest scanner in compatibility mode
        ScannerCreateInfo quest_info;
        quest_info.memory = impl_->memory.get();
        quest_info.logger = impl_->log;
        quest_info.verbose = impl_->cfg.verbose;
        quest_info.cached_regions = cached_regions;
        {
            auto quest_scanner = std::make_unique<QuestScanner>(quest_info);
            if (quest_scanner->Initialize())
            {
                impl_->scanner_manager->RegisterScanner(ScannerType::Quest, std::move(quest_scanner));
            }
            else if (impl_->log.warn)
            {
                impl_->log.warn("Failed to initialize Quest scanner in compatibility mode");
            }
        }
    }
    // Mode 2: Auto mode (hook + dialog scanner for maximum coverage)
    else
    {
        if (impl_->log.info)
            impl_->log.info("Auto mode: preparing dialog hook (deferred until warmup) and initializing dialog scanner");

        // Initialize dialog scanner in auto mode (catches cutscenes/story dialogs)
        {
            PROFILE_SCOPE_CUSTOM("Engine.InitializeDialogScanner");
            ScannerCreateInfo dialog_info;
            dialog_info.memory = impl_->memory.get();
            dialog_info.logger = impl_->log;
            dialog_info.verbose = impl_->cfg.verbose;
            dialog_info.pattern = Signatures::GetDialogPattern();
            
            auto dialog_scanner = std::make_unique<DialogScanner>(dialog_info);
            if (!dialog_scanner->Initialize() || !impl_->scanner_manager->RegisterScanner(ScannerType::Dialog, std::move(dialog_scanner)))
            {
                if (impl_->log.warn)
                    impl_->log.warn("Failed to initialize dialog scanner; will retry during polling");
            }
            else
            {
                if (impl_->log.info)
                    impl_->log.info("Dialog scanner initialized successfully");
            }
        }
    }
    
    // PHASE 2: Initialize scanners for warmup (before hooks)
    {
        PROFILE_SCOPE_CUSTOM("Engine.InitializeScannersForWarmup");
        
        // NoticeScreen scanner with warmup callback
        ScannerCreateInfo notice_info;
        notice_info.memory = impl_->memory.get();
        notice_info.logger = impl_->log;
        notice_info.verbose = impl_->cfg.verbose;
        notice_info.pattern = Signatures::GetNoticeString();
        notice_info.state_change_callback = [this](bool visible)
        {
            impl_->notice_screen_visible.store(visible, std::memory_order_release);
            // Notify warmup waiter
            {
                std::lock_guard<std::mutex> lock(impl_->warmup_mutex);
                impl_->warmup_cv.notify_all();
            }
            // Dispatch to external listeners
            std::vector<std::function<void(bool)>> listeners;
            {
                std::lock_guard<std::mutex> lock(impl_->notice_listener_mutex);
                listeners.reserve(impl_->notice_listeners.size());
                for (auto& kv : impl_->notice_listeners)
                    listeners.push_back(kv.second);
            }
            for (auto& cb : listeners)
            {
                if (cb)
                    cb(visible);
            }
        };
        
        auto notice_scanner = std::make_unique<NoticeScreenScanner>(notice_info);
        if (notice_scanner->Initialize())
        {
            impl_->scanner_manager->RegisterScanner(ScannerType::NoticeScreen, std::move(notice_scanner));
            if (impl_->log.info)
                impl_->log.info("NoticeScreen scanner initialized");
        }
        else
        {
            if (impl_->log.warn)
                impl_->log.warn("Failed to initialize NoticeScreen scanner");
        }
        
        // PostLogin scanner
        ScannerCreateInfo postlogin_info;
        postlogin_info.memory = impl_->memory.get();
        postlogin_info.logger = impl_->log;
        postlogin_info.verbose = impl_->cfg.verbose;
        postlogin_info.pattern = Signatures::GetWalkthroughPattern();
        postlogin_info.state_change_callback = [this](bool logged_in)
        {
            impl_->post_login_detected.store(logged_in, std::memory_order_release);
            // Dispatch to external listeners
            std::vector<std::function<void(bool)>> listeners;
            {
                std::lock_guard<std::mutex> lock(impl_->post_login_listener_mutex);
                listeners.reserve(impl_->post_login_listeners.size());
                for (auto& kv : impl_->post_login_listeners)
                    listeners.push_back(kv.second);
            }
            for (auto& cb : listeners)
            {
                if (cb)
                    cb(logged_in);
            }
        };
        
        auto postlogin_scanner = std::make_unique<PostLoginScanner>(postlogin_info);
        if (postlogin_scanner->Initialize())
        {
            impl_->scanner_manager->RegisterScanner(ScannerType::PostLogin, std::move(postlogin_scanner));
            if (impl_->log.info)
                impl_->log.info("PostLogin scanner initialized");
        }
        
        // PlayerName scanner for on-demand player info extraction
        ScannerCreateInfo player_info;
        player_info.memory = impl_->memory.get();
        player_info.logger = impl_->log;
        player_info.verbose = impl_->cfg.verbose;
        player_info.pattern = Signatures::GetSiblingNamePattern();
        
        auto player_scanner = std::make_unique<PlayerNameScanner>(player_info);
        if (player_scanner->Initialize())
        {
            impl_->scanner_manager->RegisterScanner(ScannerType::PlayerName, std::move(player_scanner));
        }

        ScannerCreateInfo quest_info;
        quest_info.memory = impl_->memory.get();
        quest_info.logger = impl_->log;
        quest_info.verbose = impl_->cfg.verbose;
        quest_info.cached_regions = cached_regions;
        {
            auto quest_scanner = std::make_unique<QuestScanner>(quest_info);
            if (quest_scanner->Initialize())
            {
                impl_->scanner_manager->RegisterScanner(ScannerType::Quest, std::move(quest_scanner));
            }
            else if (impl_->log.warn)
            {
                impl_->log.warn("Failed to initialize Quest scanner; will retry during polling");
            }
        }
    }

    // PHASE 3: Start scanner warmup thread (polls scanners until readiness)
    impl_->warmup_thread = std::jthread(
        [this](std::stop_token stoken)
        {
            using namespace std::chrono_literals;
            if (impl_->log.info)
                impl_->log.info("Scanner warmup thread started");

            while (!stoken.stop_requested() && !impl_->warmup_shutdown.load(std::memory_order_acquire))
            {
                // Poll notice scanner
                auto* notice_scanner = impl_->scanner_manager->GetScanner(ScannerType::NoticeScreen);
                if (notice_scanner)
                    notice_scanner->Poll();

                // Poll post-login scanner
                auto* postlogin_scanner = impl_->scanner_manager->GetScanner(ScannerType::PostLogin);
                if (postlogin_scanner)
                    postlogin_scanner->Poll();

                std::this_thread::sleep_for(50ms);
            }

            if (impl_->log.info)
                impl_->log.info("Scanner warmup thread stopped");
        });

    // PHASE 4: Wait for scanner readiness based on policy (blocks until satisfied)
    if (!impl_->WaitForScannerReadiness(policy, std::chrono::seconds(0))) // 0 = infinite wait
    {
        // Timeout or error
        impl_->warmup_shutdown.store(true, std::memory_order_release);
        impl_->warmup_thread.request_stop();
        if (impl_->warmup_thread.joinable())
            impl_->warmup_thread.join();
        
        status_ = Status::Error;
        impl_->hook_stage.store(HookStage::Idle, std::memory_order_release);
        return false;
    }

    // Stop warmup thread now that we're ready to install hooks
    impl_->warmup_shutdown.store(true, std::memory_order_release);
    impl_->warmup_thread.request_stop();
    if (impl_->warmup_thread.joinable())
        impl_->warmup_thread.join();

    // Schedule delayed patch enable after notice detection for defer policy
    if (policy == StartPolicy::DeferUntilIntegrity)
    {
        if (impl_->delayed_enable_thread.joinable())
            impl_->delayed_enable_thread.request_stop();

        impl_->delayed_enable_thread = std::jthread(
            [this](std::stop_token stoken)
            {
                using namespace std::chrono_literals;
                if (impl_->log.debug)
                    impl_->log.debug("[warmup] Scheduling hook patches to enable in 1 second");
                std::this_thread::sleep_for(1s);
                if (stoken.stop_requested())
                    return;
                if (impl_->log.debug)
                    impl_->log.debug("[warmup] Enabling hook patches after notice screen warmup");
                impl_->hook_manager.EnableAllPatches(impl_->log);
            });
    }

    // PHASE 5: Install hooks
    impl_->hook_stage.store(HookStage::InstallingHooks, std::memory_order_release);

    // Defer dialog hook installation until after warmup completes (auto mode only)
    if (!impl_->cfg.compatibility_mode)
    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallDialogHook");
        dialog_hook_installed = impl_->hook_manager.RegisterHook(
            persistence::HookType::Dialog,
            base_hook_info,
            nullptr,
            nullptr);

        if (dialog_hook_installed)
        {
            if (impl_->log.info)
                impl_->log.info("Dialog hook installed successfully (deferred)");
        }
        else
        {
            if (impl_->log.warn)
                impl_->log.warn("Failed to install dialog hook (deferred)");
        }

        auto dialog_scanner = impl_->scanner_manager->GetScanner(ScannerType::Dialog);
        if (!dialog_hook_installed && !dialog_scanner)
        {
            impl_->SetError("Failed to initialize dialog capture (both hook and scanner unavailable)");
            status_ = Status::Error;
            return false;
        }
    }

    // Do NOT pre-change page protections at startup; some builds crash on login if code pages change protection.

    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallQuestHook");
        impl_->hook_manager.RegisterHook(
            persistence::HookType::Quest,
            base_hook_info,
            nullptr,  // Integrity will be wired after integrity detour is installed
            nullptr);
    }

    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallPlayerHook");
        impl_->hook_manager.RegisterHook(
            persistence::HookType::Player,
            base_hook_info,
            nullptr,  // Integrity will be wired after integrity detour is installed
            nullptr);
    }

    // Network hook is temporarily disabled
    // Note: If enabled, register via HookManager::RegisterHook(persistence::HookType::Network, ...)

    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallCornerTextHook");
        impl_->hook_manager.RegisterHook(
            persistence::HookType::Corner,
            base_hook_info,
            nullptr,  // Integrity will be wired after integrity detour is installed
            nullptr);
    }

    // Install integrity hook through HookManager
    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallIntegrityHook");
        
        // Configure integrity-specific settings
        HookCreateInfo integrity_info = base_hook_info;
        
        bool integrity_installed = impl_->hook_manager.RegisterHook(
            persistence::HookType::Integrity,
            integrity_info,
            nullptr, nullptr);

        if (!integrity_installed)
        {
            if (impl_->log.error)
                impl_->log.error("Failed to install integrity hook");
            impl_->hook_manager.RemoveAllHooks();
            impl_->memory.reset();
            status_ = Status::Error;
            return false;
        }
        
        // Configure integrity-specific settings
        auto* integrity_hook = impl_->hook_manager.GetIntegrityHook();
        if (integrity_hook)
        {
            integrity_hook->SetDiagnosticsEnabled(impl_->cfg.enable_integrity_diagnostics);
            
            // Wire all hooks to integrity system
            impl_->hook_manager.WireIntegrityCallbacks(integrity_hook, nullptr);
        }
    }

    // Optionally enable hooks immediately based on policy
    const bool enable_patch_now = (policy == StartPolicy::EnableImmediately);
    if (enable_patch_now)
    {
        impl_->hook_manager.EnableAllPatches(impl_->log);
        
        // Proactive verification after immediate enable
        if (impl_->cfg.proactive_verify_after_enable_ms > 0)
        {
            auto delay = std::chrono::milliseconds(impl_->cfg.proactive_verify_after_enable_ms);
            std::jthread([this, delay](std::stop_token) {
                std::this_thread::sleep_for(delay);
                impl_->hook_manager.VerifyAllPatches(impl_->log, impl_->cfg.verbose);
            }).detach();
        }
    }

    // Start integrity monitor
    auto* integrity_hook = impl_->hook_manager.GetIntegrityHook();
    auto state_addr = integrity_hook ? integrity_hook->GetStateAddress() : 0;
    if (state_addr == 0)
    {
        if (impl_->log.warn)
            impl_->log.warn("No integrity state address; skipping monitor");
    }
    else
    {
        impl_->monitor = std::make_unique<dqxclarity::IntegrityMonitor>(
            impl_->memory.get(), impl_->log, state_addr,
            [this](bool first)
            {
                if (first)
                {
                    if (impl_->log.debug)
                        impl_->log.debug("[warmup] Integrity monitor first tick - enabling hook patches");
                    impl_->hook_manager.EnableAllPatches(impl_->log);
                }
                else
                {
                    if (impl_->cfg.verbose && impl_->log.info)
                        impl_->log.info("[warmup] Integrity monitor reapplying hook patches");
                    impl_->hook_manager.ReapplyAllPatches(impl_->log);
                }
            });
        
        // Wire all hooks to integrity monitor
        impl_->hook_manager.WireIntegrityCallbacks(integrity_hook, impl_->monitor.get());
        
        (void)impl_->monitor->start();
    }

    if (impl_->log.info)
        impl_->log.info("Hook installed");

    // Start poller thread to capture dialog events and publish to ring buffer
    impl_->poller = std::jthread(
        [this](std::stop_token stoken)
        {
            using namespace std::chrono_literals;
            try
            {
                while (!stoken.stop_requested())
                {
                    auto now = std::chrono::steady_clock::now();

                    // Phase 1: Capture from both sources to pending queue (no immediate publish)

                    // Hook-based capture (safe pointer capture to avoid TOCTOU race)
                    auto hook_ptr = dynamic_cast<DialogHook*>(impl_->hook_manager.GetHook(persistence::HookType::Dialog));
                    if (hook_ptr && hook_ptr->PollDialogData())
                    {
                        std::string text = hook_ptr->GetLastDialogText();
                        std::string speaker = hook_ptr->GetLastNpcName();
                        if (!text.empty())
                        {
                            // Add to pending queue
                            {
                                std::lock_guard<std::mutex> lock(impl_->pending_mutex);
                                PendingDialog pending;
                                pending.text = text;
                                pending.speaker = speaker;
                                pending.capture_time = now;
                                pending.source = PendingDialog::Hook;
                                impl_->pending_dialogs.push_back(std::move(pending));
                            }

                            // Diagnostics: track hook capture time
                            {
                                std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
                                impl_->last_capture_timings.hook_captured = now;
                                impl_->last_capture_timings.hook_valid = true;

                                // Log latency if dialog scanner captured recently
                                if (impl_->cfg.verbose && impl_->log.info &&
                                    impl_->last_capture_timings.scanner_valid)
                                {
                                    auto latency = now - impl_->last_capture_timings.scanner_captured;
                                    if (latency < 1000ms)
                                    {
                                        auto latency_ms =
                                            std::chrono::duration_cast<std::chrono::milliseconds>(latency).count();
                                        impl_->log.info("Hook captured +" + std::to_string(latency_ms) +
                                                        "ms after dialog scanner");
                                    }
                                }
                            }
                        }
                    }

                    // Parallel scanner polling using thread pool
                    struct ScannerResult
                    {
                        bool has_dialog = false;
                        std::string dialog_text;
                        std::string dialog_speaker;
                    };

                    auto dialog_future = impl_->scanner_pool->submit_task([&]() -> ScannerResult {
                        ScannerResult result;
                        auto dialog_scanner = dynamic_cast<DialogScanner*>(impl_->scanner_manager->GetScanner(ScannerType::Dialog));
                        if (dialog_scanner && dialog_scanner->Poll())
                        {
                            result.has_dialog = true;
                            result.dialog_text = dialog_scanner->GetLastDialogText();
                            result.dialog_speaker = dialog_scanner->GetLastNpcName();
                        }
                        return result;
                    });

                    auto notice_future = impl_->scanner_pool->submit_task([&]() {
                        auto notice_scanner = dynamic_cast<NoticeScreenScanner*>(impl_->scanner_manager->GetScanner(ScannerType::NoticeScreen));
                        if (notice_scanner)
                        {
                            notice_scanner->Poll();
                        }
                    });

                    auto postlogin_future = impl_->scanner_pool->submit_task([&]() {
                        auto postlogin_scanner = dynamic_cast<PostLoginScanner*>(impl_->scanner_manager->GetScanner(ScannerType::PostLogin));
                        if (postlogin_scanner)
                        {
                            postlogin_scanner->Poll();
                        }
                    });

                    ScannerResult dialog_result = dialog_future.get();
                    notice_future.get();
                    postlogin_future.get();

                    // Process dialog scanner results if captured
                    if (dialog_result.has_dialog && !dialog_result.dialog_text.empty())
                    {
                        // Add to pending queue
                        {
                            std::lock_guard<std::mutex> lock(impl_->pending_mutex);
                            PendingDialog pending;
                            pending.text = dialog_result.dialog_text;
                            pending.speaker = dialog_result.dialog_speaker;
                            pending.capture_time = now;
                            pending.source = PendingDialog::Scanner;
                            impl_->pending_dialogs.push_back(std::move(pending));
                        }

                        // Diagnostics: track dialog scanner capture time
                        {
                            std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
                            impl_->last_capture_timings.scanner_captured = now;
                            impl_->last_capture_timings.scanner_valid = true;

                            if (impl_->cfg.verbose && impl_->log.info)
                            {
                                impl_->log.info("Dialog scanner captured dialog");
                            }
                        }
                    }

                    // Phase 2: Hook-priority processing with immediate publish
                    {
                        std::lock_guard<std::mutex> lock(impl_->pending_mutex);

                        auto hook_wait_ms = std::chrono::milliseconds(impl_->cfg.hook_wait_timeout_ms);
                        std::vector<PendingDialog> ready_to_publish;

                        // Pass 1: Separate hooks and scanner results (avoids iterator invalidation)
                        std::vector<PendingDialog> hooks;
                        std::vector<PendingDialog> scanner_results;

                        for (auto& dialog : impl_->pending_dialogs)
                        {
                            if (dialog.source == PendingDialog::Hook)
                            {
                                hooks.push_back(std::move(dialog));
                            }
                            else
                            {
                                scanner_results.push_back(std::move(dialog));
                            }
                        }
                        impl_->pending_dialogs.clear();

                        // Pass 2: Process hooks and mark scanner duplicates
                        std::set<size_t> scanner_indices_to_skip;

                        for (auto& hook : hooks)
                        {
                            for (size_t i = 0; i < scanner_results.size(); ++i)
                            {
                                if (scanner_results[i].text == hook.text)
                                {
                                    // Found scanner duplicate - hook upgrades it
                                    if (impl_->cfg.verbose && impl_->log.info)
                                    {
                                        auto upgrade_latency = now - scanner_results[i].capture_time;
                                        auto latency_ms =
                                            std::chrono::duration_cast<std::chrono::milliseconds>(upgrade_latency)
                                                .count();
                                        impl_->log.info("Hook upgraded scanner capture (+" +
                                                        std::to_string(latency_ms) + "ms, has NPC name)");
                                    }
                                    scanner_indices_to_skip.insert(i);
                                    break;
                                }
                            }

                            // Publish hook immediately (higher priority, has NPC name)
                            ready_to_publish.push_back(std::move(hook));
                        }

                        // Pass 3: Process scanner timeouts (skip those upgraded by hooks)
                        for (size_t i = 0; i < scanner_results.size(); ++i)
                        {
                            if (scanner_indices_to_skip.count(i) > 0)
                            {
                                continue; // Skip - was upgraded by hook
                            }

                            auto age = now - scanner_results[i].capture_time;
                            if (age >= hook_wait_ms)
                            {
                                // Scanner timeout - hook didn't capture, publish scanner version
                                if (impl_->cfg.verbose && impl_->log.info)
                                {
                                    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
                                    impl_->log.debug("Scanner timeout (waited " + std::to_string(wait_ms) +
                                                     "ms, hook didn't capture)");
                                }
                                ready_to_publish.push_back(std::move(scanner_results[i]));
                            }
                            else
                            {
                                // Not timed out yet - put back in pending queue
                                impl_->pending_dialogs.push_back(std::move(scanner_results[i]));
                            }
                        }

                        // Cleanup expired entries from global cache
                        {
                            std::lock_guard<std::mutex> cache_lock(impl_->cache_mutex);
                            auto cache_it = impl_->published_cache.begin();
                            while (cache_it != impl_->published_cache.end())
                            {
                                auto cache_age = now - cache_it->publish_time;
                                if (cache_age > impl_->CACHE_EXPIRY_MS)
                                {
                                    cache_it = impl_->published_cache.erase(cache_it);
                                }
                                else
                                {
                                    ++cache_it;
                                }
                            }
                        }

                        // Publish dialogs to ring buffer (with global cache check)
                        for (auto& dialog : ready_to_publish)
                        {
                            // Check global cache to prevent cross-batch duplicates
                            bool is_duplicate = false;
                            {
                                std::lock_guard<std::mutex> cache_lock(impl_->cache_mutex);
                                for (const auto& cached : impl_->published_cache)
                                {
                                    if (cached.text == dialog.text)
                                    {
                                        is_duplicate = true;
                                        if (impl_->cfg.verbose && impl_->log.info)
                                        {
                                            impl_->log.info("Blocked duplicate dialog (found in global cache)");
                                        }
                                        break;
                                    }
                                }
                            }

                            if (is_duplicate)
                            {
                                continue; // Skip publishing this duplicate
                            }

                            // Publish to ring buffers
                            DialogMessage msg;
                            msg.seq = ++impl_->seq;
                            msg.text = dialog.text;
                            msg.speaker = dialog.speaker;
                            msg.lang.clear();
                            impl_->ring.try_push(std::move(msg));

                            // Add to global cache
                            {
                                std::lock_guard<std::mutex> cache_lock(impl_->cache_mutex);
                                impl_->published_cache.push_back({ dialog.text, now });
                            }

                            // Diagnostics: log publication latency
                            if (impl_->cfg.verbose && impl_->log.info)
                            {
                                std::lock_guard<std::mutex> diag_lock(impl_->diagnostics_mutex);
                                auto capture_to_publish = now - dialog.capture_time;
                                auto capture_ms =
                                    std::chrono::duration_cast<std::chrono::milliseconds>(capture_to_publish).count();

                                impl_->log.info(
                                    "Published dialog (source: " +
                                    std::string(dialog.source == PendingDialog::Hook ? "Hook" : "Scanner") +
                                    ", latency: " + std::to_string(capture_ms) + "ms)");
                            }
                        }
                    }
                    // Quest polling (hook + parallel scanner)
                    {
                        auto quest_hook_ptr = dynamic_cast<QuestHook*>(impl_->hook_manager.GetHook(persistence::HookType::Quest));
                        if (quest_hook_ptr && quest_hook_ptr->PollQuestData())
                        {
                            const auto& q = quest_hook_ptr->GetLastQuest();
                            PendingQuest pq;
                            pq.key = q.quest_name;
                            pq.subquest_name = q.subquest_name;
                            pq.quest_name = q.quest_name;
                            pq.description = q.description;
                            pq.rewards = q.rewards;
                            pq.repeat_rewards = q.repeat_rewards;
                            pq.capture_time = now;
                            pq.source = PendingQuest::Hook;
                            {
                                std::lock_guard<std::mutex> ql(impl_->pending_quest_mutex);
                                impl_->pending_quests.push_back(std::move(pq));
                            }
                        }

                        struct QuestScanResult
                        {
                            bool has_quest = false;
                            std::string key;
                            std::string subquest_name;
                            std::string quest_name;
                            std::string description;
                        };

                        auto quest_scanner_future = impl_->scanner_pool->submit_task([&]() -> QuestScanResult {
                            QuestScanResult result;
                            auto quest_scanner = dynamic_cast<QuestScanner*>(impl_->scanner_manager->GetScanner(ScannerType::Quest));
                            if (quest_scanner && quest_scanner->Poll())
                            {
                                result.has_quest = true;
                                result.key = quest_scanner->GetLastQuestName();
                                result.subquest_name = quest_scanner->GetLastSubquestName();
                                result.quest_name = quest_scanner->GetLastQuestName();
                                result.description = quest_scanner->GetLastDescription();
                            }
                            return result;
                        });

                        QuestScanResult quest_result = quest_scanner_future.get();

                        if (quest_result.has_quest && !quest_result.key.empty())
                        {
                            PendingQuest pq;
                            pq.key = quest_result.key;
                            pq.subquest_name = quest_result.subquest_name;
                            pq.quest_name = quest_result.quest_name;
                            pq.description = quest_result.description;
                            pq.rewards.clear();
                            pq.repeat_rewards.clear();
                            pq.capture_time = now;
                            pq.source = PendingQuest::Scanner;
                            {
                                std::lock_guard<std::mutex> ql(impl_->pending_quest_mutex);
                                impl_->pending_quests.push_back(std::move(pq));
                            }
                        }
                    }
                    // Player hook polling (captures player data)
                    auto player_hook_ptr = dynamic_cast<PlayerHook*>(impl_->hook_manager.GetHook(persistence::HookType::Player));
                    if (player_hook_ptr && player_hook_ptr->PollPlayerData())
                    {
                        PlayerInfo info = player_hook_ptr->GetLastPlayer();
                        update_player_info(std::move(info));
                    }
                    // Network hook polling (safe pointer capture to avoid TOCTOU race)
                    auto network_hook_ptr = dynamic_cast<NetworkTextHook*>(impl_->hook_manager.GetHook(persistence::HookType::Network));
                    if (network_hook_ptr)
                    {
                        (void)network_hook_ptr->PollNetworkText();
                    }
                    // Corner hook polling (safe pointer capture to avoid TOCTOU race)
                    auto corner_hook_ptr = dynamic_cast<CornerTextHook*>(impl_->hook_manager.GetHook(persistence::HookType::Corner));
                    if (corner_hook_ptr && corner_hook_ptr->PollCornerText())
                    {
                        const std::string& captured = corner_hook_ptr->GetLastText();
                        if (!captured.empty())
                        {
                            CornerTextItem corner_item;
                            corner_item.seq = impl_->corner_text_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;
                            corner_item.text = captured;
                            impl_->corner_text_ring.try_push(std::move(corner_item));
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(impl_->pending_quest_mutex);
                        auto hook_wait_ms = std::chrono::milliseconds(impl_->cfg.hook_wait_timeout_ms);

                        std::vector<PendingQuest> hooks;
                        std::vector<PendingQuest> scanner_results;
                        for (auto& q : impl_->pending_quests)
                        {
                            if (q.source == PendingQuest::Hook)
                                hooks.push_back(std::move(q));
                            else
                                scanner_results.push_back(std::move(q));
                        }
                        impl_->pending_quests.clear();

                        std::set<size_t> scanner_indices_to_skip;
                        std::vector<PendingQuest> ready;

                        for (auto& h : hooks)
                        {
                            for (size_t i = 0; i < scanner_results.size(); ++i)
                            {
                                if (scanner_results[i].key == h.key)
                                {
                                    scanner_indices_to_skip.insert(i);
                                    break;
                                }
                            }
                            ready.push_back(std::move(h));
                        }

                        for (size_t i = 0; i < scanner_results.size(); ++i)
                        {
                            if (scanner_indices_to_skip.count(i) > 0)
                                continue;
                            auto age = now - scanner_results[i].capture_time;
                            if (age >= hook_wait_ms)
                                ready.push_back(std::move(scanner_results[i]));
                            else
                                impl_->pending_quests.push_back(std::move(scanner_results[i]));
                        }

                        // Cleanup quest cache
                        {
                            std::lock_guard<std::mutex> cache_lock(impl_->quest_cache_mutex);
                            auto it = impl_->quest_published_cache.begin();
                            while (it != impl_->quest_published_cache.end())
                            {
                                auto cache_age = now - it->publish_time;
                                if (cache_age > impl_->QUEST_CACHE_EXPIRY_MS)
                                    it = impl_->quest_published_cache.erase(it);
                                else
                                    ++it;
                            }
                        }

                        for (auto& q : ready)
                        {
                            bool dup = false;
                            {
                                std::lock_guard<std::mutex> cache_lock(impl_->quest_cache_mutex);
                                for (const auto& cached : impl_->quest_published_cache)
                                {
                                    if (cached.key == q.key)
                                    {
                                        dup = true;
                                        break;
                                    }
                                }
                            }
                            if (dup)
                                continue;

                            QuestMessage snapshot;
                            snapshot.subquest_name = q.subquest_name;
                            snapshot.quest_name = q.quest_name;
                            snapshot.description = q.description;
                            snapshot.rewards = q.rewards;
                            snapshot.repeat_rewards = q.repeat_rewards;
                            snapshot.seq = impl_->quest_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;

                            {
                                std::lock_guard<std::mutex> lock2(impl_->quest_mutex);
                                impl_->quest_snapshot = std::move(snapshot);
                                impl_->quest_valid = true;
                            }

                            {
                                std::lock_guard<std::mutex> cache_lock(impl_->quest_cache_mutex);
                                impl_->quest_published_cache.push_back({ q.key, now });
                            }
                        }
                    }

                    std::this_thread::sleep_for(100ms);
                }
            }
            catch (const std::exception& e)
            {
                if (impl_->log.error)
                {
                    impl_->log.error("Polling thread crashed with exception: " + std::string(e.what()));
                }
                status_ = Status::Error;
            }
            catch (...)
            {
                if (impl_->log.error)
                {
                    impl_->log.error("Polling thread crashed with unknown exception");
                }
                status_ = Status::Error;
            }
        });

    // PHASE 8: Mark as ready
    status_ = Status::Hooked;
    impl_->hook_stage.store(HookStage::Ready, std::memory_order_release);

    if (impl_->log.info)
        impl_->log.info("Engine started successfully - all systems operational");

    return true;
}

bool Engine::stop_hook()
{
    if (status_ == Status::Stopped || status_ == Status::Stopping)
        return true;
    status_ = Status::Stopping;

    try
    {
        // Stop delayed enable thread if running
        impl_->delayed_enable_thread.request_stop();
        if (impl_->delayed_enable_thread.joinable())
            impl_->delayed_enable_thread.join();

        // Stop warmup thread if running
        impl_->warmup_shutdown.store(true, std::memory_order_release);
        if (impl_->warmup_thread.joinable())
        {
            impl_->warmup_thread.request_stop();
            impl_->warmup_thread.join();
        }

        impl_->poller.request_stop();
        if (impl_->poller.joinable())
            impl_->poller.join();

        {
            std::lock_guard<std::mutex> lock(impl_->quest_mutex);
            impl_->quest_valid = false;
        }
        {
            std::lock_guard<std::mutex> lock(impl_->player_mutex);
            impl_->player_valid = false;
        }

        if (impl_->monitor)
        {
            impl_->monitor->stop();
            impl_->monitor.reset();
        }
        
        // Remove all hooks via HookManager (handles cleanup and persistence unregistration)
        impl_->hook_manager.RemoveAllHooks();
        
        impl_->memory.reset();
        if (impl_->log.info)
            impl_->log.info("Hook removed");
        status_ = Status::Stopped;
        impl_->hook_stage.store(HookStage::Idle, std::memory_order_release);

        // Clear hook registry after successful cleanup
        try
        {
            persistence::HookRegistry::ClearRegistry();
        }
        catch (const std::exception& e)
        {
            if (impl_->log.warn)
                impl_->log.warn(std::string("Failed to clear hook registry: ") + e.what());
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (impl_->log.error)
        {
            impl_->log.error("Exception during hook cleanup: " + std::string(e.what()));
        }
        status_ = Status::Error;
        return false;
    }
    catch (...)
    {
        if (impl_->log.error)
        {
            impl_->log.error("Unknown exception during hook cleanup");
        }
        status_ = Status::Error;
        return false;
    }
}

bool Engine::drain(std::vector<DialogMessage>& out) { return impl_->ring.pop_all(out) > 0; }

bool Engine::drainCornerText(std::vector<CornerTextItem>& out) { return impl_->corner_text_ring.pop_all(out) > 0; }

bool Engine::latest_quest(QuestMessage& out) const
{
    std::lock_guard<std::mutex> lock(impl_->quest_mutex);
    if (!impl_->quest_valid)
    {
        return false;
    }
    out = impl_->quest_snapshot;
    return true;
}

bool Engine::latest_player(PlayerInfo& out) const
{
    std::lock_guard<std::mutex> lock(impl_->player_mutex);
    if (!impl_->player_valid)
    {
        return false;
    }
    out = impl_->player_snapshot;
    return true;
}

void Engine::update_player_info(PlayerInfo info)
{
    std::string log_player;
    std::string log_sibling;
    if (impl_->cfg.verbose && impl_->log.info)
    {
        log_player = info.player_name;
        log_sibling = info.sibling_name;
    }

    info.seq = impl_->player_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;
    {
        std::lock_guard<std::mutex> lock(impl_->player_mutex);
        impl_->player_snapshot = std::move(info);
        impl_->player_valid = true;
    }

    if (impl_->cfg.verbose && impl_->log.info)
    {
        impl_->log.info("Player info updated: player=\"" + log_player + "\" sibling=\"" + log_sibling + "\"");
    }
}

bool Engine::isNoticeScreenVisible() const
{
    return impl_->notice_screen_visible.load(std::memory_order_acquire);
}

bool Engine::isPostLoginDetected() const
{
    return impl_->post_login_detected.load(std::memory_order_acquire);
}

bool Engine::scanPlayerInfo(PlayerInfo& out)
{
    if (!impl_->scanner_manager)
        return false;
        
    auto player_scanner = dynamic_cast<PlayerNameScanner*>(impl_->scanner_manager->GetScanner(ScannerType::PlayerName));
    if (!player_scanner)
        return false;
        
    return player_scanner->ScanPlayerInfo(out);
}

Engine::NoticeListenerId Engine::addNoticeStateListener(std::function<void(bool)> callback)
{
    if (!callback)
        return 0;
    std::lock_guard<std::mutex> lock(impl_->notice_listener_mutex);
    auto id = impl_->notice_listener_seq.fetch_add(1, std::memory_order_relaxed);
    impl_->notice_listeners[id] = std::move(callback);
    return id;
}

void Engine::removeNoticeStateListener(Engine::NoticeListenerId id)
{
    if (id == 0)
        return;
    std::lock_guard<std::mutex> lock(impl_->notice_listener_mutex);
    impl_->notice_listeners.erase(id);
}

Engine::PostLoginListenerId Engine::addPostLoginStateListener(std::function<void(bool)> callback)
{
    if (!callback)
        return 0;
    std::lock_guard<std::mutex> lock(impl_->post_login_listener_mutex);
    auto id = impl_->post_login_listener_seq.fetch_add(1, std::memory_order_relaxed);
    impl_->post_login_listeners[id] = std::move(callback);
    return id;
}

void Engine::removePostLoginStateListener(Engine::PostLoginListenerId id)
{
    if (id == 0)
        return;
    std::lock_guard<std::mutex> lock(impl_->post_login_listener_mutex);
    impl_->post_login_listeners.erase(id);
}

EngineState Engine::state() const
{
    EngineState s;
    s.status = status_;
    s.hook_stage = impl_->hook_stage.load(std::memory_order_acquire);
    return s;
}

std::string Engine::last_error() const
{
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->last_error_message;
}

} // namespace dqxclarity

