#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dqxclarity {

enum class Status { Stopped, Starting, Hooked, Stopping, Error };

struct QuestMessage;
struct DialogStreamItem;

struct Config {
  bool verbose = false;
  bool console_output = false; // kept for parity; not used by the library
  bool defer_dialog_patch = true; // enable initial patch only after first integrity
  bool instruction_safe_steal = true; // compute stolen bytes safely
  int readback_bytes = 16; // how many bytes to log from patch sites
  int proactive_verify_after_enable_ms = 200; // recheck patch post-enable
  bool enable_post_login_heuristics = false; // optional walkthrough heuristic
  // Diagnostic switch: when true, emits detailed integrity scanning diagnostics.
  bool enable_integrity_diagnostics = false;
};

struct Logger {
  std::function<void(const std::string&)> info;
  std::function<void(const std::string&)> warn;
  std::function<void(const std::string&)> error;
};

class Engine {
 public:
  Engine();
  ~Engine();

  enum class StartPolicy { DeferUntilIntegrity, EnableImmediately };

  bool initialize(const Config& cfg, Logger loggers = {});
  bool start_hook();
  bool start_hook(StartPolicy policy);
  bool stop_hook();
  Status status() const { return status_; }

  // Drain all available dialog messages into out (single consumer)
  bool drain(std::vector<struct DialogMessage>& out);
  bool drainStream(std::vector<struct DialogStreamItem>& out);

  bool latest_quest(struct QuestMessage& out) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Status status_ = Status::Stopped;
};

} // namespace dqxclarity
