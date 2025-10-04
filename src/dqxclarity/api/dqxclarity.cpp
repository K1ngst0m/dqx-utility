#include "dqxclarity.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../process/ProcessFinder.hpp"
#include "../hooking/DialogHook.hpp"

#include <chrono>
#include <thread>

namespace dqxclarity {

struct Engine::Impl {
  Config cfg{};
  Logger log{};
  std::shared_ptr<IProcessMemory> memory;
  std::unique_ptr<DialogHook> hook;
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

  // Install the dialog hook
  impl_->hook = std::make_unique<dqxclarity::DialogHook>(impl_->memory);
  impl_->hook->SetVerbose(impl_->cfg.verbose);
  if (!impl_->hook->InstallHook()) {
    if (impl_->log.error) impl_->log.error("Failed to install dialog hook");
    impl_->hook.reset();
    impl_->memory.reset();
    status_ = Status::Error;
    return false;
  }

  if (impl_->log.info) impl_->log.info("Hook installed");
  status_ = Status::Hooked;
  return true;
}

bool Engine::stop_hook() {
  if (status_ == Status::Stopped || status_ == Status::Stopping) return true;
  status_ = Status::Stopping;
  if (impl_->hook) {
    impl_->hook->RemoveHook();
    impl_->hook.reset();
  }
  impl_->memory.reset();
  if (impl_->log.info) impl_->log.info("Hook removed");
  status_ = Status::Stopped;
  return true;
}

} // namespace dqxclarity
