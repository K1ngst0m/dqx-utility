#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../process/ProcessFinder.hpp"
#include "../hooking/DialogHook.hpp"
#include "../hooking/IntegrityDetour.hpp"
#include "../hooking/IntegrityMonitor.hpp"
#include "dialog_message.hpp"
#include "../util/spsc_ring.hpp"

#include <chrono>
#include <thread>
#include <atomic>

namespace dqxclarity {

struct Engine::Impl {
  Config cfg{};
  Logger log{};
  std::shared_ptr<IProcessMemory> memory;
  std::unique_ptr<DialogHook> hook;
  std::unique_ptr<class IntegrityDetour> integrity;

  SpscRing<DialogMessage, 1024> ring;
  std::atomic<std::uint64_t> seq{0};
  std::thread poller;
  std::atomic<bool> poll_stop{false};
  std::unique_ptr<class IntegrityMonitor> monitor;
};

Engine::Engine() : impl_(new Impl{}) {}
Engine::~Engine() { stop_hook(); }

bool Engine::initialize(const Config& cfg, Logger loggers) {
  impl_->cfg = cfg;
  impl_->log = std::move(loggers);
  status_ = Status::Stopped;
  return true;
}

bool Engine::start_hook() {
  return start_hook(StartPolicy{ impl_->cfg.defer_dialog_patch ? StartPolicy::DeferUntilIntegrity
                                                               : StartPolicy::EnableImmediately });
}

bool Engine::start_hook(StartPolicy policy) {
  if (status_ == Status::Hooked || status_ == Status::Starting) return true;
  status_ = Status::Starting;

  // Find DQXGame.exe
  auto pids = dqxclarity::ProcessFinder::FindByName("DQXGame.exe", false);
  if (pids.empty()) {
    if (impl_->log.error) impl_->log.error("DQXGame.exe not found");
    status_ = Status::Error;
    return false;
  }

  // Create memory interface and attach
  auto mem_unique = dqxclarity::MemoryFactory::CreatePlatformMemory();
  impl_->memory = std::shared_ptr<IProcessMemory>(std::move(mem_unique));
  if (!impl_->memory || !impl_->memory->AttachProcess(pids[0])) {
    if (impl_->log.error) impl_->log.error("Failed to attach to DQXGame.exe");
    status_ = Status::Error;
    return false;
  }

  // Prepare the dialog hook FIRST but do not enable patch yet (we need its original bytes)
  impl_->hook = std::make_unique<dqxclarity::DialogHook>(impl_->memory);
  impl_->hook->SetVerbose(impl_->cfg.verbose);
  impl_->hook->SetLogger(impl_->log);
  impl_->hook->SetInstructionSafeSteal(impl_->cfg.instruction_safe_steal);
  impl_->hook->SetReadbackBytes(static_cast<size_t>(impl_->cfg.readback_bytes));
  if (!impl_->hook->InstallHook(/*enable_patch=*/false)) {
    if (impl_->log.error) impl_->log.error("Failed to prepare dialog hook");
    impl_->hook.reset();
    status_ = Status::Error;
    return false;
  }
  // Do NOT change page protection at startup (keeps login stable on this build)

  // Do NOT pre-change page protections at startup; some builds crash on login if code pages change protection.

  // Install integrity detour and configure it to restore dialog hook bytes during checks
  impl_->integrity = std::make_unique<dqxclarity::IntegrityDetour>(impl_->memory);
  impl_->integrity->SetVerbose(impl_->cfg.verbose);
  impl_->integrity->SetLogger(impl_->log);
  impl_->integrity->SetDiagnosticsEnabled(impl_->cfg.enable_integrity_diagnostics);
  // Provide restoration info so integrity trampoline can unhook temporarily
  if (impl_->hook && impl_->hook->GetHookAddress() != 0) {
    impl_->integrity->AddRestoreTarget(impl_->hook->GetHookAddress(), impl_->hook->GetOriginalBytes());
  }
  if (!impl_->integrity->Install()) {
    if (impl_->log.error) impl_->log.error("Failed to install integrity detour");
    impl_->integrity.reset();
    impl_->hook.reset();
    impl_->memory.reset();
    status_ = Status::Error;
    return false;
  }

  // Optionally enable the dialog hook immediately (will be restored during integrity)
  const bool enable_patch_now = (policy == StartPolicy::EnableImmediately);
  if (enable_patch_now) {
    (void)impl_->hook->EnablePatch();
  }

  // Proactive verification after immediate enable
  if (enable_patch_now && impl_->cfg.proactive_verify_after_enable_ms > 0) {
    auto delay = std::chrono::milliseconds(impl_->cfg.proactive_verify_after_enable_ms);
    std::thread([this, delay]{
      std::this_thread::sleep_for(delay);
      if (!impl_->hook) return;
      if (!impl_->hook->IsPatched()) {
        if (impl_->log.warn) impl_->log.warn("Post-enable verify: hook not present; reapplying once");
        (void)impl_->hook->ReapplyPatch();
      } else {
        if (impl_->log.info) impl_->log.info("Post-enable verify: hook present");
      }
    }).detach();
  }

  // Start integrity monitor to enable/reapply dialog hook
  auto state_addr = impl_->integrity ? impl_->integrity->GetStateAddress() : 0;
  if (state_addr == 0) {
    if (impl_->log.warn) impl_->log.warn("No integrity state address; skipping monitor");
  } else {
    impl_->monitor = std::make_unique<dqxclarity::IntegrityMonitor>(impl_->memory, impl_->log, state_addr,
      [this](bool first){
        if (!impl_->hook) return;
        if (first) {
          (void)impl_->hook->EnablePatch();
          if (impl_->log.info) impl_->log.info("Dialog hook enabled after first integrity run");
        } else {
          (void)impl_->hook->ReapplyPatch();
          if (impl_->log.info) impl_->log.info("Dialog hook re-applied after integrity");
        }
      }
    );
    // Provide restore targets (dialog hook site and original bytes) to monitor for out-of-process restore
    if (impl_->hook && impl_->hook->GetHookAddress() != 0) {
      impl_->monitor->AddRestoreTarget(impl_->hook->GetHookAddress(), impl_->hook->GetOriginalBytes());
    }
    (void)impl_->monitor->start();
  }

  if (impl_->log.info) impl_->log.info("Hook installed");

  // Start poller thread to capture dialog events and publish to ring buffer
  impl_->poll_stop.store(false);
  impl_->poller = std::thread([this]{
    using namespace std::chrono_literals;
    while (!impl_->poll_stop.load()) {
      if (impl_->hook && impl_->hook->PollDialogData()) {
        DialogMessage msg;
        msg.seq = ++impl_->seq;
        msg.text = impl_->hook->GetLastDialogText();
        msg.speaker = impl_->hook->GetLastNpcName();
        msg.lang.clear();
        if (!msg.text.empty()) {
          impl_->ring.try_push(std::move(msg));
        }
      }
      std::this_thread::sleep_for(100ms);
    }
  });

  status_ = Status::Hooked;
  return true;
}

bool Engine::stop_hook() {
  if (status_ == Status::Stopped || status_ == Status::Stopping) return true;
  status_ = Status::Stopping;
  impl_->poll_stop.store(true);
  if (impl_->poller.joinable()) impl_->poller.join();


  if (impl_->monitor) {
    impl_->monitor->stop();
    impl_->monitor.reset();
  }
  if (impl_->hook) {
    impl_->hook->RemoveHook();
    impl_->hook.reset();
  }
  if (impl_->integrity) {
    impl_->integrity->Remove();
    impl_->integrity.reset();
  }
  impl_->memory.reset();
  if (impl_->log.info) impl_->log.info("Hook removed");
  status_ = Status::Stopped;
  return true;
}

bool Engine::drain(std::vector<DialogMessage>& out) {
  return impl_->ring.pop_all(out) > 0;
}

} // namespace dqxclarity
