#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
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

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

namespace dqxclarity
{

struct Engine::Impl
{
    Config cfg{};
    Logger log{};
    std::shared_ptr<IProcessMemory> memory;
    std::unique_ptr<DialogHook> hook;
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
    return true;
}

bool Engine::start_hook()
{
    return start_hook(StartPolicy{ impl_->cfg.defer_dialog_patch ? StartPolicy::DeferUntilIntegrity :
                                                                   StartPolicy::EnableImmediately });
}

bool Engine::start_hook(StartPolicy policy)
{
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

    // Prepare the dialog hook FIRST but do not enable patch yet (we need its original bytes)
    impl_->hook = std::make_unique<dqxclarity::DialogHook>(impl_->memory);
    impl_->hook->SetVerbose(impl_->cfg.verbose);
    impl_->hook->SetLogger(impl_->log);
    impl_->hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
    impl_->hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
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
    if (!impl_->hook->InstallHook(/*enable_patch=*/false))
    {
        if (impl_->log.error)
            impl_->log.error("Failed to prepare dialog hook");
        impl_->hook.reset();
        status_ = Status::Error;
        return false;
    }
    // Do NOT change page protection at startup (keeps login stable on this build)

    // Do NOT pre-change page protections at startup; some builds crash on login if code pages change protection.

    impl_->quest_hook = std::make_unique<dqxclarity::QuestHook>(impl_->memory);
    impl_->quest_hook->SetVerbose(impl_->cfg.verbose);
    impl_->quest_hook->SetLogger(impl_->log);
    impl_->quest_hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
    impl_->quest_hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
    if (impl_->quest_hook && !impl_->quest_hook->InstallHook(/*enable_patch=*/false))
    {
        if (impl_->log.warn)
            impl_->log.warn("Failed to prepare quest hook; continuing without quest capture");
        impl_->quest_hook.reset();
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

    impl_->corner_hook = std::make_unique<dqxclarity::CornerTextHook>(impl_->memory);
    impl_->corner_hook->SetVerbose(impl_->cfg.verbose);
    impl_->corner_hook->SetLogger(impl_->log);
    impl_->corner_hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
    impl_->corner_hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
    if (impl_->corner_hook && !impl_->corner_hook->InstallHook(/*enable_patch=*/false))
    {
        if (impl_->log.warn)
            impl_->log.warn("Failed to prepare corner text hook; continuing without capture");
        impl_->corner_hook.reset();
    }

    // Install integrity detour and configure it to restore dialog hook bytes during checks
    impl_->integrity = std::make_unique<dqxclarity::IntegrityDetour>(impl_->memory);
    impl_->integrity->SetVerbose(impl_->cfg.verbose);
    impl_->integrity->SetLogger(impl_->log);
    impl_->integrity->SetDiagnosticsEnabled(impl_->cfg.enable_integrity_diagnostics);
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
                if (impl_->hook && impl_->hook->PollDialogData())
                {
                    std::string text = impl_->hook->GetLastDialogText();
                    std::string speaker = impl_->hook->GetLastNpcName();
                    if (!text.empty())
                    {
                        DialogMessage msg;
                        msg.seq = ++impl_->seq;
                        msg.text = text;
                        msg.speaker = speaker;
                        msg.lang.clear();
                        impl_->ring.try_push(std::move(msg));

                        DialogStreamItem stream_item;
                        stream_item.seq = impl_->stream_seq.fetch_add(1, std::memory_order_relaxed) + 1ull;
                        stream_item.type = DialogStreamType::Dialog;
                        stream_item.text = std::move(text);
                        stream_item.speaker = std::move(speaker);
                        impl_->stream_ring.try_push(std::move(stream_item));
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
