#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../api/dqxclarity.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace dqxclarity
{

class IntegrityMonitor
{
public:
    struct RestoreSite
    {
        uintptr_t addr;
        std::vector<uint8_t> bytes;
    };

    IntegrityMonitor(IProcessMemory* memory, Logger logger, uintptr_t state_addr,
                     std::function<void(bool first)> on_integrity)
        : memory_(memory)
        , log_(std::move(logger))
        , state_addr_(state_addr)
        , on_integrity_(std::move(on_integrity))
    {
    }

    void AddRestoreTarget(uintptr_t addr, const std::vector<uint8_t>& bytes)
    {
        std::lock_guard<std::mutex> lock(restore_mutex_);
        for (auto& site : restore_)
        {
            if (site.addr == addr)
            {
                site.bytes = bytes;
                return;
            }
        }
        restore_.push_back({ addr, bytes });
    }

    void UpdateRestoreTarget(uintptr_t addr, const std::vector<uint8_t>& bytes) { AddRestoreTarget(addr, bytes); }

    void MoveRestoreTarget(uintptr_t old_addr, uintptr_t new_addr, const std::vector<uint8_t>& bytes)
    {
        std::lock_guard<std::mutex> lock(restore_mutex_);
        for (auto& site : restore_)
        {
            if (site.addr == old_addr)
            {
                site.addr = new_addr;
                site.bytes = bytes;
                return;
            }
        }
        restore_.push_back({ new_addr, bytes });
    }

    bool start();
    void stop();

private:
    IProcessMemory* memory_;
    Logger log_{};
    uintptr_t state_addr_ = 0;
    std::function<void(bool)> on_integrity_;

    std::vector<RestoreSite> restore_;
    mutable std::mutex restore_mutex_;

    std::thread worker_;
    std::atomic<bool> stop_{ false };
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace dqxclarity