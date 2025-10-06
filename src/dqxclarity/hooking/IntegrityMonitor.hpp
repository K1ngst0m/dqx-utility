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
    struct RestoreSite { uintptr_t addr; std::vector<uint8_t> bytes; };

    IntegrityMonitor(std::shared_ptr<IProcessMemory> memory,
                     Logger logger,
                     uintptr_t state_addr,
                     std::function<void(bool first)> on_integrity)
        : memory_(std::move(memory))
        , log_(std::move(logger))
        , state_addr_(state_addr)
        , on_integrity_(std::move(on_integrity)) {}

    void AddRestoreTarget(uintptr_t addr, const std::vector<uint8_t>& bytes) {
        restore_.push_back({addr, bytes});
    }

    bool start();
    void stop();

private:
    std::shared_ptr<IProcessMemory> memory_;
    Logger log_{};
    uintptr_t state_addr_ = 0;
    std::function<void(bool)> on_integrity_;

    std::vector<RestoreSite> restore_;

    std::thread worker_;
    std::atomic<bool> stop_{false};
};

} // namespace dqxclarity