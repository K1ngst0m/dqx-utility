#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../api/dqxclarity.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace dqxclarity {

class IntegrityMonitor {
public:
    IntegrityMonitor(std::shared_ptr<IProcessMemory> memory,
                     Logger logger,
                     uintptr_t state_addr,
                     std::function<void(bool first)> on_integrity)
        : memory_(std::move(memory))
        , log_(std::move(logger))
        , state_addr_(state_addr)
        , on_integrity_(std::move(on_integrity)) {}

    bool start();
    void stop();

private:
    std::shared_ptr<IProcessMemory> memory_;
    Logger log_{};
    uintptr_t state_addr_ = 0;
    std::function<void(bool)> on_integrity_;

    std::thread worker_;
    std::atomic<bool> stop_{false};
};

} // namespace dqxclarity