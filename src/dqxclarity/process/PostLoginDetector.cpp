#include "PostLoginDetector.hpp"

#include "../memory/MemoryFactory.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../pattern/PatternScanner.hpp"
#include "../signatures/Signatures.hpp"
#include "../process/ProcessFinder.hpp"

#include <thread>

namespace dqxclarity {

bool DetectPostLogin(
    std::atomic<bool>& cancel,
    std::chrono::milliseconds poll_interval,
    std::chrono::milliseconds timeout)
{
    auto pids = ProcessFinder::FindByName("DQXGame.exe", false);
    if (pids.empty()) return false;

    auto mem_unique = MemoryFactory::CreatePlatformMemory();
    std::shared_ptr<IProcessMemory> memory(std::move(mem_unique));
    if (!memory || !memory->AttachProcess(pids[0])) return false;

    PatternScanner scanner(memory);
    const auto& pat = Signatures::GetWalkthroughPattern();

    auto start = std::chrono::steady_clock::now();
    while (!cancel.load()) {
        // Search across readable regions (data), not just executable
        auto found = scanner.ScanProcess(pat, /*require_executable=*/false);
        if (found.has_value()) {
            memory->DetachProcess();
            return true;
        }
        if (timeout.count() > 0 && (std::chrono::steady_clock::now() - start) > timeout) {
            break;
        }
        std::this_thread::sleep_for(poll_interval);
    }

    memory->DetachProcess();
    return false;
}

} // namespace dqxclarity