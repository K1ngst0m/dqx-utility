#include "IntegrityMonitor.hpp"

#include <chrono>
#include <thread>

namespace dqxclarity
{

bool IntegrityMonitor::start()
{
    if (!memory_ || !memory_->IsProcessAttached() || state_addr_ == 0 || worker_.joinable())
        return false;
    stop_.store(false);
    worker_ = std::thread(
        [this]
        {
            using namespace std::chrono_literals;
            bool first = true;
            uint32_t hits = 0;
            while (!stop_.load())
            {
                uint8_t flag = 0;
                if (memory_->ReadMemory(state_addr_, &flag, 1) && flag == 1)
                {
                    // Out-of-process restore of hook sites immediately on signal
                    std::vector<RestoreSite> restore_copy;
                    {
                        std::lock_guard<std::mutex> lock(restore_mutex_);
                        restore_copy = restore_;
                    }
                    for (const auto& site : restore_copy)
                    {
                        if (!site.bytes.empty())
                        {
                            (void)memory_->WriteMemory(site.addr, site.bytes.data(), site.bytes.size());
                        }
                    }
                    ++hits;
                    if (log_.info)
                        log_.info(std::string("Integrity signal observed; hits=") + std::to_string(hits) +
                                  "; restoring");
                    // Delay before re-applying the dialog hook to avoid racing the checker
                    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                    if (on_integrity_)
                        on_integrity_(first);
                    first = false;
                    uint8_t zero = 0;
                    (void)memory_->WriteMemory(state_addr_, &zero, 1);
                }
                std::this_thread::sleep_for(1ms);
            }
        });
    return true;
}

void IntegrityMonitor::stop()
{
    stop_.store(true);
    if (worker_.joinable())
        worker_.join();
}

} // namespace dqxclarity