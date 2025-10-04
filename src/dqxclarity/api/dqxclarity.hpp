#pragma once

#include <functional>
#include <memory>
#include <string>

namespace dqxclarity {

enum class Status { Stopped, Starting, Hooked, Stopping, Error };

struct Config {
  bool verbose = false;
  bool console_output = false; // kept for parity; not used by the library
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

  bool initialize(const Config& cfg, Logger loggers = {});
  bool start_hook();
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
