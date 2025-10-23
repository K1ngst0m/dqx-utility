#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../memory/DialogMemoryReader.hpp"
#include "../process/ProcessFinder.hpp"
#include "../hooking/DialogHook.hpp"
#include "../hooking/CornerTextHook.hpp"
#include "../hooking/NetworkTextHook.hpp"
#include "../hooking/QuestHook.hpp"
#include "../hooking/IntegrityDetour.hpp"
#include "../hooking/IntegrityMonitor.hpp"
#include "dialog_message.hpp"
#include "dialog_stream.hpp"
#include "quest_message.hpp"
#include "../util/SPSCRing.hpp"
#include "../util/Profile.hpp"
#include "../pattern/MemoryRegion.hpp"

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>

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
        MemoryReader
    } source;
};

struct Engine::Impl
{
    Config cfg{};
    Logger log{};
    std::shared_ptr<IProcessMemory> memory;
    std::unique_ptr<DialogHook> hook;
    std::unique_ptr<DialogMemoryReader> memory_reader; // Alternative non-invasive dialog reader
    std::unique_ptr<QuestHook> quest_hook;
    std::unique_ptr<NetworkTextHook> network_hook;
    std::unique_ptr<CornerTextHook> corner_hook;
    std::unique_ptr<class IntegrityDetour> integrity;

    SpscRing<DialogMessage, 1024> ring;
    std::atomic<std::uint64_t> seq{ 0 };
    SpscRing<DialogStreamItem, 1024> stream_ring;
    std::atomic<std::uint64_t> stream_seq{ 0 };
    std::thread poller;
    std::atomic<bool> poll_stop{ false };
    std::unique_ptr<class IntegrityMonitor> monitor;

    std::atomic<std::uint64_t> quest_seq{ 0 };
    mutable std::mutex quest_mutex;
    QuestMessage quest_snapshot;
    bool quest_valid = false;

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
    static constexpr std::chrono::milliseconds CACHE_EXPIRY_MS{5000}; // 5 seconds

    // Diagnostics: latency tracking for both capture methods
    struct CaptureTimings
    {
        std::chrono::steady_clock::time_point hook_captured;
        std::chrono::steady_clock::time_point memory_reader_captured;
        bool hook_valid = false;
        bool memory_reader_valid = false;
    } last_capture_timings;
    std::mutex diagnostics_mutex;
};

Engine::Engine()
    : impl_(new Impl{})
{
}

Engine::~Engine() { stop_hook(); }

bool Engine::initialize(const Config& cfg, Logger loggers)
{
    impl_->cfg = cfg;
    impl_->log = std::move(loggers);
    status_ = Status::Stopped;

#if DQX_PROFILING_LEVEL >= 1
    // Set profiling logger to route profiling output through dqxclarity's Logger
    profiling::SetProfilingLogger(&impl_->log);
#endif

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
        auto mem_unique = dqxclarity::MemoryFactory::CreatePlatformMemory();
        impl_->memory = std::shared_ptr<IProcessMemory>(std::move(mem_unique));
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

    // Initialize dialog capture based on mode
    bool hook_installed = false;

    // Mode 1: Compatibility mode (memory reader only, safer)
    if (impl_->cfg.compatibility_mode)
    {
        if (impl_->log.info)
            impl_->log.info("Compatibility mode: using memory reader only (no hooking)");

        PROFILE_SCOPE_CUSTOM("Engine.InitializeMemoryReader");
        impl_->memory_reader = std::make_unique<dqxclarity::DialogMemoryReader>(impl_->memory);
        impl_->memory_reader->SetVerbose(impl_->cfg.verbose);
        impl_->memory_reader->SetLogger(impl_->log);
        if (!impl_->memory_reader->Initialize())
        {
            if (impl_->log.error)
                impl_->log.error("Failed to initialize memory reader in compatibility mode");
            status_ = Status::Error;
            return false;
        }
        if (impl_->log.info)
            impl_->log.info("Memory reader initialized successfully (compatibility mode)");
    }
    // Mode 2: Auto mode (hook + memory reader for maximum coverage)
    else
    {
        if (impl_->log.info)
            impl_->log.info("Auto mode: initializing hook + memory reader for maximum coverage");

        // Try to install hook (non-fatal if it fails)
        {
            PROFILE_SCOPE_CUSTOM("Engine.InstallDialogHook");
            impl_->hook = std::make_unique<dqxclarity::DialogHook>(impl_->memory);
            impl_->hook->SetVerbose(impl_->cfg.verbose);
            impl_->hook->SetLogger(impl_->log);
            impl_->hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
            impl_->hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
            impl_->hook->SetCachedRegions(cached_regions);
            impl_->hook->SetOriginalBytesChangedCallback(
                [this](uintptr_t addr, const std::vector<uint8_t>& bytes)
                {
                    if (impl_->integrity)
                    {
                        impl_->integrity->UpdateRestoreTarget(addr, bytes);
                    }
                    if (impl_->monitor)
                    {
                        impl_->monitor->UpdateRestoreTarget(addr, bytes);
                    }
                });
            impl_->hook->SetHookSiteChangedCallback(
                [this](uintptr_t old_addr, uintptr_t new_addr, const std::vector<uint8_t>& bytes)
                {
                    if (impl_->integrity)
                    {
                        impl_->integrity->MoveRestoreTarget(old_addr, new_addr, bytes);
                    }
                    if (impl_->monitor)
                    {
                        impl_->monitor->MoveRestoreTarget(old_addr, new_addr, bytes);
                    }
                });
            if (impl_->hook->InstallHook(/*enable_patch=*/false))
            {
                hook_installed = true;
                if (impl_->log.info)
                    impl_->log.info("Dialog hook installed successfully");
            }
            else
            {
                if (impl_->log.warn)
                    impl_->log.warn("Failed to install dialog hook; continuing with memory reader only");
                impl_->hook.reset();
            }
        }

        // Always initialize memory reader in auto mode (catches cutscenes/story dialogs)
        {
            PROFILE_SCOPE_CUSTOM("Engine.InitializeMemoryReader");
            impl_->memory_reader = std::make_unique<dqxclarity::DialogMemoryReader>(impl_->memory);
            impl_->memory_reader->SetVerbose(impl_->cfg.verbose);
            impl_->memory_reader->SetLogger(impl_->log);
            if (!impl_->memory_reader->Initialize())
            {
                if (impl_->log.warn)
                    impl_->log.warn("Failed to initialize memory reader; will retry during polling");
            }
            else
            {
                if (impl_->log.info)
                    impl_->log.info("Memory reader initialized successfully");
            }
        }

        // In auto mode, we need at least one method to work
        if (!hook_installed && !impl_->memory_reader)
        {
            if (impl_->log.error)
                impl_->log.error("Failed to initialize dialog capture (both hook and memory reader unavailable)");
            status_ = Status::Error;
            return false;
        }
    }
    // Do NOT change page protection at startup (keeps login stable on this build)

    // Do NOT pre-change page protections at startup; some builds crash on login if code pages change protection.

    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallQuestHook");
        impl_->quest_hook = std::make_unique<dqxclarity::QuestHook>(impl_->memory);
        impl_->quest_hook->SetVerbose(impl_->cfg.verbose);
        impl_->quest_hook->SetLogger(impl_->log);
        impl_->quest_hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
        impl_->quest_hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
        impl_->quest_hook->SetCachedRegions(cached_regions);
        if (impl_->quest_hook && !impl_->quest_hook->InstallHook(/*enable_patch=*/false))
        {
            if (impl_->log.warn)
                impl_->log.warn("Failed to prepare quest hook; continuing without quest capture");
            impl_->quest_hook.reset();
        }
    }

    // temporarily disabled due to unused
#if 0
  impl_->network_hook = std::make_unique<dqxclarity::NetworkTextHook>(impl_->memory);
  impl_->network_hook->SetVerbose(impl_->cfg.verbose);
  impl_->network_hook->SetLogger(impl_->log);
  impl_->network_hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
  impl_->network_hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
  if (impl_->network_hook && !impl_->network_hook->InstallHook(/*enable_patch=*/false)) {
    if (impl_->log.warn) impl_->log.warn("Failed to prepare network text hook; continuing without capture");
    impl_->network_hook.reset();
  }
#endif

    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallCornerTextHook");
        impl_->corner_hook = std::make_unique<dqxclarity::CornerTextHook>(impl_->memory);
        impl_->corner_hook->SetVerbose(impl_->cfg.verbose);
        impl_->corner_hook->SetLogger(impl_->log);
        impl_->corner_hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
        impl_->corner_hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
        impl_->corner_hook->SetCachedRegions(cached_regions);
        if (impl_->corner_hook && !impl_->corner_hook->InstallHook(/*enable_patch=*/false))
        {
            if (impl_->log.warn)
                impl_->log.warn("Failed to prepare corner text hook; continuing without capture");
            impl_->corner_hook.reset();
        }
    }

    // Install integrity detour and configure it to restore dialog hook bytes during checks
    {
        PROFILE_SCOPE_CUSTOM("Engine.InstallIntegrityDetour");
        impl_->integrity = std::make_unique<dqxclarity::IntegrityDetour>(impl_->memory);
        impl_->integrity->SetVerbose(impl_->cfg.verbose);
        impl_->integrity->SetLogger(impl_->log);
        impl_->integrity->SetDiagnosticsEnabled(impl_->cfg.enable_integrity_diagnostics);
        impl_->integrity->SetCachedRegions(cached_regions);
        // Provide restoration info so integrity trampoline can unhook temporarily
        if (impl_->hook && impl_->hook->GetHookAddress() != 0)
        {
            impl_->integrity->AddRestoreTarget(impl_->hook->GetHookAddress(), impl_->hook->GetOriginalBytes());
        }
        if (impl_->quest_hook && impl_->quest_hook->GetHookAddress() != 0)
        {
            impl_->integrity->AddRestoreTarget(impl_->quest_hook->GetHookAddress(), impl_->quest_hook->GetOriginalBytes());
        }
        if (impl_->network_hook && impl_->network_hook->GetHookAddress() != 0)
        {
            impl_->integrity->AddRestoreTarget(impl_->network_hook->GetHookAddress(),
                                               impl_->network_hook->GetOriginalBytes());
        }
        if (impl_->corner_hook && impl_->corner_hook->GetHookAddress() != 0)
        {
            impl_->integrity->AddRestoreTarget(impl_->corner_hook->GetHookAddress(),
                                               impl_->corner_hook->GetOriginalBytes());
        }
        if (!impl_->integrity->Install())
        {
            if (impl_->log.error)
                impl_->log.error("Failed to install integrity detour");
            impl_->integrity.reset();
            if (impl_->quest_hook)
            {
                impl_->quest_hook->RemoveHook();
                impl_->quest_hook.reset();
            }
            if (impl_->network_hook)
            {
                impl_->network_hook->RemoveHook();
                impl_->network_hook.reset();
            }
            if (impl_->corner_hook)
            {
                impl_->corner_hook->RemoveHook();
                impl_->corner_hook.reset();
            }
            impl_->hook.reset();
            impl_->memory.reset();
            status_ = Status::Error;
            return false;
        }
    }

    // Optionally enable the dialog hook immediately (will be restored during integrity)
    const bool enable_patch_now = (policy == StartPolicy::EnableImmediately);
    if (enable_patch_now)
    {
        if (impl_->hook)
            (void)impl_->hook->EnablePatch();
        if (impl_->quest_hook)
            (void)impl_->quest_hook->EnablePatch();
        if (impl_->network_hook)
            (void)impl_->network_hook->EnablePatch();
        if (impl_->corner_hook)
            (void)impl_->corner_hook->EnablePatch();
    }

    // Proactive verification after immediate enable
    if (enable_patch_now && impl_->cfg.proactive_verify_after_enable_ms > 0)
    {
        auto delay = std::chrono::milliseconds(impl_->cfg.proactive_verify_after_enable_ms);
        std::thread(
            [this, delay]
            {
                std::this_thread::sleep_for(delay);
                if (impl_->hook)
                {
                    if (!impl_->hook->IsPatched())
                    {
                        if (impl_->log.warn)
                            impl_->log.warn("Post-enable verify: dialog hook not present; reapplying once");
                        (void)impl_->hook->ReapplyPatch();
                    }
                    else
                    {
                        if (impl_->log.info)
                            impl_->log.info("Post-enable verify: dialog hook present");
                    }
                }
                if (impl_->quest_hook)
                {
                    if (!impl_->quest_hook->IsPatched())
                    {
                        if (impl_->log.warn)
                            impl_->log.warn("Post-enable verify: quest hook not present; reapplying once");
                        (void)impl_->quest_hook->ReapplyPatch();
                    }
                }
                if (impl_->network_hook)
                {
                    if (!impl_->network_hook->IsPatched())
                    {
                        if (impl_->log.warn)
                            impl_->log.warn("Post-enable verify: network text hook not present; reapplying once");
                        (void)impl_->network_hook->ReapplyPatch();
                    }
                    else
                    {
                        if (impl_->log.info)
                            impl_->log.info("Post-enable verify: network text hook present");
                    }
                }
                if (impl_->corner_hook)
                {
                    if (!impl_->corner_hook->IsPatched())
                    {
                        if (impl_->log.warn)
                            impl_->log.warn("Post-enable verify: corner text hook not present; reapplying once");
                        (void)impl_->corner_hook->ReapplyPatch();
                    }
                    else
                    {
                        if (impl_->log.info)
                            impl_->log.info("Post-enable verify: corner text hook present");
                    }
                }
            })
            .detach();
    }

    // Start integrity monitor to enable/reapply dialog hook
    auto state_addr = impl_->integrity ? impl_->integrity->GetStateAddress() : 0;
    if (state_addr == 0)
    {
        if (impl_->log.warn)
            impl_->log.warn("No integrity state address; skipping monitor");
    }
    else
    {
        impl_->monitor = std::make_unique<dqxclarity::IntegrityMonitor>(
            impl_->memory, impl_->log, state_addr,
            [this](bool first)
            {
                if (first)
                {
                    if (impl_->hook)
                    {
                        (void)impl_->hook->EnablePatch();
                        if (impl_->log.info)
                            impl_->log.info("Dialog hook enabled after first integrity run");
                    }
                    if (impl_->quest_hook)
                    {
                        (void)impl_->quest_hook->EnablePatch();
                        if (impl_->log.info)
                            impl_->log.info("Quest hook enabled after first integrity run");
                    }
                    if (impl_->network_hook)
                    {
                        (void)impl_->network_hook->EnablePatch();
                        if (impl_->log.info)
                            impl_->log.info("Network text hook enabled after first integrity run");
                    }
                    if (impl_->corner_hook)
                    {
                        (void)impl_->corner_hook->EnablePatch();
                        if (impl_->log.info)
                            impl_->log.info("Corner text hook enabled after first integrity run");
                    }
                }
                else
                {
                    if (impl_->hook)
                    {
                        (void)impl_->hook->ReapplyPatch();
                        if (impl_->log.info)
                            impl_->log.info("Dialog hook re-applied after integrity");
                    }
                    if (impl_->quest_hook)
                    {
                        (void)impl_->quest_hook->ReapplyPatch();
                        if (impl_->log.info)
                            impl_->log.info("Quest hook re-applied after integrity");
                    }
                    if (impl_->network_hook)
                    {
                        (void)impl_->network_hook->ReapplyPatch();
                        if (impl_->log.info)
                            impl_->log.info("Network text hook re-applied after integrity");
                    }
                    if (impl_->corner_hook)
                    {
                        (void)impl_->corner_hook->ReapplyPatch();
                        if (impl_->log.info)
                            impl_->log.info("Corner text hook re-applied after integrity");
                    }
                }
            });
        // Provide restore targets (dialog hook site and original bytes) to monitor for out-of-process restore
        if (impl_->hook && impl_->hook->GetHookAddress() != 0)
        {
            impl_->monitor->AddRestoreTarget(impl_->hook->GetHookAddress(), impl_->hook->GetOriginalBytes());
        }
        if (impl_->quest_hook && impl_->quest_hook->GetHookAddress() != 0)
        {
            impl_->monitor->AddRestoreTarget(impl_->quest_hook->GetHookAddress(),
                                             impl_->quest_hook->GetOriginalBytes());
        }
        if (impl_->network_hook && impl_->network_hook->GetHookAddress() != 0)
        {
            impl_->monitor->AddRestoreTarget(impl_->network_hook->GetHookAddress(),
                                             impl_->network_hook->GetOriginalBytes());
        }
        if (impl_->corner_hook && impl_->corner_hook->GetHookAddress() != 0)
        {
            impl_->monitor->AddRestoreTarget(impl_->corner_hook->GetHookAddress(),
                                             impl_->corner_hook->GetOriginalBytes());
        }
        (void)impl_->monitor->start();
    }

    if (impl_->log.info)
        impl_->log.info("Hook installed");

    // Start poller thread to capture dialog events and publish to ring buffer
    impl_->poll_stop.store(false);
    impl_->poller = std::thread(
        [this]
        {
            using namespace std::chrono_literals;
            while (!impl_->poll_stop.load())
            {
                auto now = std::chrono::steady_clock::now();

                // Phase 1: Capture from both sources to pending queue (no immediate publish)

                // Hook-based capture
                if (impl_->hook && impl_->hook->PollDialogData())
                {
                    std::string text = impl_->hook->GetLastDialogText();
                    std::string speaker = impl_->hook->GetLastNpcName();
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

                            // Log latency if memory reader captured recently
                            if (impl_->cfg.verbose && impl_->log.info &&
                                impl_->last_capture_timings.memory_reader_valid)
                            {
                                auto latency = now - impl_->last_capture_timings.memory_reader_captured;
                                if (latency < 1000ms)
                                {
                                    auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(latency).count();
                                    impl_->log.info("Hook captured +" + std::to_string(latency_ms) +
                                                   "ms after memory reader");
                                }
                            }
                        }
                    }
                }

                // Memory reader capture
                if (impl_->memory_reader && impl_->memory_reader->PollDialogData())
                {
                    std::string text = impl_->memory_reader->GetLastDialogText();
                    std::string speaker = impl_->memory_reader->GetLastNpcName();
                    if (!text.empty())
                    {
                        // Add to pending queue
                        {
                            std::lock_guard<std::mutex> lock(impl_->pending_mutex);
                            PendingDialog pending;
                            pending.text = text;
                            pending.speaker = speaker;
                            pending.capture_time = now;
                            pending.source = PendingDialog::MemoryReader;
                            impl_->pending_dialogs.push_back(std::move(pending));
                        }

                        // Diagnostics: track memory reader capture time
                        {
                            std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
                            impl_->last_capture_timings.memory_reader_captured = now;
                            impl_->last_capture_timings.memory_reader_valid = true;

                            if (impl_->cfg.verbose && impl_->log.info)
                            {
                                impl_->log.info("Memory reader captured dialog");
                            }
                        }
                    }
                }

                // Phase 2: Process pending queue 
                {
                    std::lock_guard<std::mutex> lock(impl_->pending_mutex);

                    // Find items ready for publication
                    std::vector<PendingDialog> ready;
                    auto it = impl_->pending_dialogs.begin();
                    while (it != impl_->pending_dialogs.end())
                    {
                        auto age = now - it->capture_time;
                        if (age >= 400ms)
                        {
                            ready.push_back(std::move(*it));
                            it = impl_->pending_dialogs.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    // Deduplicate by text (prefer hook for NPC name)
                    std::map<std::string, PendingDialog> unique;
                    for (auto& dialog : ready)
                    {
                        auto map_it = unique.find(dialog.text);
                        if (map_it == unique.end())
                        {
                            unique[dialog.text] = std::move(dialog);
                        }
                        else
                        {
                            // Duplicate found: prefer hook (has NPC name)
                            if (dialog.source == PendingDialog::Hook)
                            {
                                unique[dialog.text] = std::move(dialog);
                            }
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

                    // Publish unique dialogs to ring buffer (with global cache check)
                    for (auto& [text_key, dialog] : unique)
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

                        DialogStreamItem stream_item;
                        stream_item.seq = impl_->stream_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;
                        stream_item.type = DialogStreamType::Dialog;
                        stream_item.text = dialog.text;
                        stream_item.speaker = dialog.speaker;
                        impl_->stream_ring.try_push(std::move(stream_item));

                        // Add to global cache
                        {
                            std::lock_guard<std::mutex> cache_lock(impl_->cache_mutex);
                            impl_->published_cache.push_back({dialog.text, now});
                        }

                        // Diagnostics: log publication latency
                        if (impl_->cfg.verbose && impl_->log.info)
                        {
                            std::lock_guard<std::mutex> diag_lock(impl_->diagnostics_mutex);
                            auto capture_to_publish = now - dialog.capture_time;
                            auto capture_ms = std::chrono::duration_cast<std::chrono::milliseconds>(capture_to_publish).count();

                            impl_->log.info("Published dialog (source: " +
                                           std::string(dialog.source == PendingDialog::Hook ? "Hook" : "MemoryReader") +
                                           ", latency: " + std::to_string(capture_ms) + "ms)");
                        }
                    }
                }
                if (impl_->quest_hook && impl_->quest_hook->PollQuestData())
                {
                    QuestMessage snapshot;
                    const auto& quest = impl_->quest_hook->GetLastQuest();
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
                if (impl_->network_hook)
                {
                    (void)impl_->network_hook->PollNetworkText();
                }
                if (impl_->corner_hook && impl_->corner_hook->PollCornerText())
                {
                    const std::string& captured = impl_->corner_hook->GetLastText();
                    if (!captured.empty())
                    {
                        DialogStreamItem stream_item;
                        stream_item.seq = impl_->stream_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;
                        stream_item.type = DialogStreamType::CornerText;
                        stream_item.text = captured;
                        stream_item.speaker.clear();
                        impl_->stream_ring.try_push(std::move(stream_item));
                    }
                }
                std::this_thread::sleep_for(100ms);
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
    impl_->poll_stop.store(true);
    if (impl_->poller.joinable())
        impl_->poller.join();

    {
        std::lock_guard<std::mutex> lock(impl_->quest_mutex);
        impl_->quest_valid = false;
    }

    if (impl_->monitor)
    {
        impl_->monitor->stop();
        impl_->monitor.reset();
    }
    if (impl_->quest_hook)
    {
        impl_->quest_hook->RemoveHook();
        impl_->quest_hook.reset();
    }
    if (impl_->network_hook)
    {
        impl_->network_hook->RemoveHook();
        impl_->network_hook.reset();
    }
    if (impl_->corner_hook)
    {
        impl_->corner_hook->RemoveHook();
        impl_->corner_hook.reset();
    }
    if (impl_->hook)
    {
        impl_->hook->RemoveHook();
        impl_->hook.reset();
    }
    if (impl_->integrity)
    {
        impl_->integrity->Remove();
        impl_->integrity.reset();
    }
    impl_->memory.reset();
    if (impl_->log.info)
        impl_->log.info("Hook removed");
    status_ = Status::Stopped;
    return true;
}

bool Engine::drain(std::vector<DialogMessage>& out) { return impl_->ring.pop_all(out) > 0; }

bool Engine::drainStream(std::vector<DialogStreamItem>& out) { return impl_->stream_ring.pop_all(out) > 0; }

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

} // namespace dqxclarity
