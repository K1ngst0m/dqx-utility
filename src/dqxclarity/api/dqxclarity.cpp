#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../process/ProcessFinder.hpp"
#include "../hooking/DialogHook.hpp"
#include "../hooking/IntegrityDetour.hpp"
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
  std::thread hider;
  std::atomic<bool> hide_stop{false};
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

  // Install integrity detour BEFORE enabling dialog hook
  impl_->integrity = std::make_unique<dqxclarity::IntegrityDetour>(impl_->memory);
  impl_->integrity->SetVerbose(impl_->cfg.verbose);
  impl_->integrity->SetLogger(impl_->log);
  if (!impl_->integrity->Install()) {
    if (impl_->log.error) impl_->log.error("Failed to install integrity detour");
    impl_->integrity.reset();
    impl_->memory.reset();
    status_ = Status::Error;
    return false;
  }

  // Prepare the dialog hook but DEFER patching until first integrity signal
  impl_->hook = std::make_unique<dqxclarity::DialogHook>(impl_->memory);
  impl_->hook->SetVerbose(impl_->cfg.verbose);
  impl_->hook->SetLogger(impl_->log);
  if (!impl_->hook->InstallHook(/*enable_patch=*/false)) {
    if (impl_->log.error) impl_->log.error("Failed to install dialog hook");
    impl_->hook.reset();
    impl_->integrity->Remove();
    impl_->integrity.reset();
    impl_->memory.reset();
    status_ = Status::Error;
    return false;
  }

  // Start hook hide/reapply thread (monitor integrity state byte)
  impl_->hide_stop.store(false);
  impl_->hider = std::thread([this]{
    using namespace std::chrono_literals;
    auto state_addr = impl_->integrity ? impl_->integrity->GetStateAddress() : 0;
    if (state_addr == 0) return; // nothing to do
    bool first_enabled = false;
    while (!impl_->hide_stop.load()) {
      uint8_t flag = 0;
      if (impl_->memory && impl_->memory->ReadMemory(state_addr, &flag, 1) && flag == 1) {
        if (impl_->log.info) impl_->log.info("Integrity signal observed; scheduling reapply/enable");
        // Give the game a moment, then enable or reapply our JMP
        std::this_thread::sleep_for(1s);
        if (impl_->hook) {
          if (!first_enabled) {
            (void)impl_->hook->EnablePatch();
            first_enabled = true;
            if (impl_->log.info) impl_->log.info("Dialog hook enabled after first integrity run");
          } else {
            (void)impl_->hook->ReapplyPatch();
            if (impl_->log.info) impl_->log.info("Dialog hook re-applied after integrity");
          }
        }
        uint8_t zero = 0;
        (void)impl_->memory->WriteMemory(state_addr, &zero, 1);
      }
      std::this_thread::sleep_for(250ms);
    }
  });

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
        msg.speaker.clear();
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

  impl_->hide_stop.store(true);
  if (impl_->hider.joinable()) impl_->hider.join();

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
