#include "IntegrityMonitor.hpp"

#include <chrono>
#include <thread>

namespace dqxclarity {

bool IntegrityMonitor::start() {
    if (!memory_ || !memory_->IsProcessAttached() || state_addr_ == 0 || worker_.joinable()) return false;
    stop_.store(false);
    worker_ = std::thread([this]{
        using namespace std::chrono_literals;
        bool first = true;
        while (!stop_.load()) {
            uint8_t flag = 0;
            if (memory_->ReadMemory(state_addr_, &flag, 1) && flag == 1) {
                if (log_.info) log_.info(first ? "Integrity signal observed; scheduling first enable"
                                               : "Integrity signal observed; scheduling reapply");
                std::this_thread::sleep_for(1s);
                if (on_integrity_) on_integrity_(first);
                first = false;
                uint8_t zero = 0; (void)memory_->WriteMemory(state_addr_, &zero, 1);
            }
            std::this_thread::sleep_for(250ms);
        }
    });
    return true;
}

void IntegrityMonitor::stop() {
    stop_.store(true);
    if (worker_.joinable()) worker_.join();
}

} // namespace dqxclarity