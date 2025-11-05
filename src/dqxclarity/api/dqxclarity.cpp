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

namespace dqxclarity
{

// Helper to create dialog pattern (from DialogMemoryReader)
static Pattern CreateDialogPattern()
{
    // Pattern: \xFF\xFF\xFF\x7F\xFF\xFF\xFF\x7F\x00\x00\x00\x00\x00\x00\x00\x00\xFD.\xA8\x99
    // Note: byte at index 17 is wildcard (0xFF)
    static constexpr uint8_t kDialogPatternBytes[] = {
        0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xFF, 0xA8, 0x99
    };
    static constexpr size_t kPatternSize = sizeof(kDialogPatternBytes);
    
    Pattern pattern;
    pattern.bytes.assign(kDialogPatternBytes, kDialogPatternBytes + kPatternSize);
    pattern.mask.assign(kPatternSize, true);
    pattern.mask[17] = false; // Wildcard at index 17
    
    return pattern;
}

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

    // Scanner state tracking
    std::atomic<bool> notice_screen_visible{ false };
    std::atomic<bool> post_login_detected{ false };

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

    // Diagnostics: latency tracking for both capture methods
    struct CaptureTimings
    {
        std::chrono::steady_clock::time_point hook_captured;
        std::chrono::steady_clock::time_point scanner_captured;
        bool hook_valid = false;
        bool scanner_valid = false;
    } last_capture_timings;

    std::mutex diagnostics_mutex;
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

    // Find DQXGame.exe
    {
        PROFILE_SCOPE_CUSTOM("Engine.FindProcess");
        auto pids = dqxclarity::ProcessFinder::FindByName("DQXGame.exe", false);
        if (pids.empty())
        {
            if (impl_->log.error)
                impl_->log.error("DQXGame.exe not found");
            status_ = Status::Error;
            return false;
        }

        // Create memory interface and attach
        impl_->memory = dqxclarity::MemoryFactory::CreatePlatformMemory();
        if (!impl_->memory || !impl_->memory->AttachProcess(pids[0]))
        {
            if (impl_->log.error)
                impl_->log.error("Failed to attach to DQXGame.exe");
            status_ = Status::Error;
            return false;
        }
    }

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

    // Initialize dialog capture based on mode
    bool hook_installed = false;

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
        dialog_info.pattern = CreateDialogPattern();
        
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
    }
    // Mode 2: Auto mode (hook + dialog scanner for maximum coverage)
    else
    {
        if (impl_->log.info)
            impl_->log.info("Auto mode: initializing hook + dialog scanner for maximum coverage");

        // Try to install dialog hook (non-fatal if it fails)
        // Note: Integrity callbacks will be wired after integrity is created
        {
            PROFILE_SCOPE_CUSTOM("Engine.InstallDialogHook");
            hook_installed = impl_->hook_manager.RegisterHook(
                persistence::HookType::Dialog,
                base_hook_info,
                nullptr,  // Integrity will be wired after integrity detour is installed
                nullptr);
        }

        // Always initialize dialog scanner in auto mode (catches cutscenes/story dialogs)
        {
            PROFILE_SCOPE_CUSTOM("Engine.InitializeDialogScanner");
            ScannerCreateInfo dialog_info;
            dialog_info.memory = impl_->memory.get();
            dialog_info.logger = impl_->log;
            dialog_info.verbose = impl_->cfg.verbose;
            dialog_info.pattern = CreateDialogPattern();
            
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

        // In auto mode, we need at least one method to work
        auto dialog_scanner = impl_->scanner_manager->GetScanner(ScannerType::Dialog);
        if (!hook_installed && !dialog_scanner)
        {
            if (impl_->log.error)
                impl_->log.error("Failed to initialize dialog capture (both hook and scanner unavailable)");
            status_ = Status::Error;
            return false;
        }
    }
    
    // Register other scanners (NoticeScreen, PostLogin, PlayerName)
    {
        PROFILE_SCOPE_CUSTOM("Engine.InitializeOtherScanners");
        
        // NoticeScreen scanner for detecting loading screen
        ScannerCreateInfo notice_info;
        notice_info.memory = impl_->memory.get();
        notice_info.logger = impl_->log;
        notice_info.verbose = impl_->cfg.verbose;
        notice_info.pattern = Signatures::GetNoticeString();
        
        auto notice_scanner = std::make_unique<NoticeScreenScanner>(notice_info);
        if (notice_scanner->Initialize())
        {
            impl_->scanner_manager->RegisterScanner(ScannerType::NoticeScreen, std::move(notice_scanner));
        }
        
        // PostLogin scanner for detecting logged-in state
        ScannerCreateInfo postlogin_info;
        postlogin_info.memory = impl_->memory.get();
        postlogin_info.logger = impl_->log;
        postlogin_info.verbose = impl_->cfg.verbose;
        postlogin_info.pattern = Signatures::GetWalkthroughPattern();
        
        auto postlogin_scanner = std::make_unique<PostLoginScanner>(postlogin_info);
        if (postlogin_scanner->Initialize())
        {
            impl_->scanner_manager->RegisterScanner(ScannerType::PostLogin, std::move(postlogin_scanner));
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
    }
    // Do NOT change page protection at startup (keeps login stable on this build)

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
                    impl_->hook_manager.EnableAllPatches(impl_->log);
                else
                    impl_->hook_manager.ReapplyAllPatches(impl_->log);
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

                    // Dialog scanner capture (safe pointer capture to avoid TOCTOU race)
                    auto dialog_scanner = dynamic_cast<DialogScanner*>(impl_->scanner_manager->GetScanner(ScannerType::Dialog));
                    if (dialog_scanner && dialog_scanner->Poll())
                    {
                        std::string text = dialog_scanner->GetLastDialogText();
                        std::string speaker = dialog_scanner->GetLastNpcName();
                        if (!text.empty())
                        {
                            // Add to pending queue
                            {
                                std::lock_guard<std::mutex> lock(impl_->pending_mutex);
                                PendingDialog pending;
                                pending.text = text;
                                pending.speaker = speaker;
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
                    }
                    
                    // Poll other scanners and update state
                    {
                        auto notice_scanner = dynamic_cast<NoticeScreenScanner*>(impl_->scanner_manager->GetScanner(ScannerType::NoticeScreen));
                        if (notice_scanner)
                        {
                            notice_scanner->Poll();
                            impl_->notice_screen_visible.store(notice_scanner->IsVisible(), std::memory_order_release);
                        }
                        
                        auto postlogin_scanner = dynamic_cast<PostLoginScanner*>(impl_->scanner_manager->GetScanner(ScannerType::PostLogin));
                        if (postlogin_scanner)
                        {
                            postlogin_scanner->Poll();
                            impl_->post_login_detected.store(postlogin_scanner->IsLoggedIn(), std::memory_order_release);
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
                    // Quest hook polling (safe pointer capture to avoid TOCTOU race)
                    auto quest_hook_ptr = dynamic_cast<QuestHook*>(impl_->hook_manager.GetHook(persistence::HookType::Quest));
                    if (quest_hook_ptr && quest_hook_ptr->PollQuestData())
                    {
                        QuestMessage snapshot;
                        const auto& quest = quest_hook_ptr->GetLastQuest();
                        snapshot.subquest_name = quest.subquest_name;
                        snapshot.quest_name = quest.quest_name;
                        snapshot.description = quest.description;
                        snapshot.rewards = quest.rewards;
                        snapshot.repeat_rewards = quest.repeat_rewards;
                        snapshot.seq = impl_->quest_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;

                        {
                            std::lock_guard<std::mutex> lock(impl_->quest_mutex);
                            impl_->quest_snapshot = std::move(snapshot);
                            impl_->quest_valid = true;
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

    status_ = Status::Hooked;
    return true;
}

bool Engine::stop_hook()
{
    if (status_ == Status::Stopped || status_ == Status::Stopping)
        return true;
    status_ = Status::Stopping;

    try
    {
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

} // namespace dqxclarity

