#pragma once

#include <functional>
#include <memory>
#include <string>

namespace dqxclarity {

enum class Status { Stopped, Starting, Hooked, Stopping, Error };

struct Config {
  bool verbose = false;
  bool console_output = false; // kept for parity; not used by the library
  bool defer_dialog_patch = true; // enable initial patch only after first integrity
  bool instruction_safe_steal = true; // compute stolen bytes safely
  int readback_bytes = 16; // how many bytes to log from patch sites
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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Status status_ = Status::Stopped;
};

} // namespace dqxclarity
