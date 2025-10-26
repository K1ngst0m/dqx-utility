#include <utility>
#include "IMemoryScanner.hpp"
#include "PatternScanner.hpp"
#include "../memory/IProcessMemory.hpp"

namespace dqxclarity
{

ProcessMemoryScanner::ProcessMemoryScanner(std::shared_ptr<IProcessMemory> memory)
    : memory_(std::move(memory))
{
}

std::optional<uintptr_t> ProcessMemoryScanner::ScanProcess(const Pattern& pattern, bool require_executable)
{
    if (!memory_ || !memory_->IsProcessAttached())
    {
        return std::nullopt;
    }

    PatternScanner scanner(memory_);
    return scanner.ScanProcess(pattern, require_executable);
}

std::vector<uintptr_t> ProcessMemoryScanner::ScanProcessAll(const Pattern& pattern, bool require_executable)
{
    if (!memory_ || !memory_->IsProcessAttached())
    {
        return {};
    }

    PatternScanner scanner(memory_);
    return scanner.ScanProcessAll(pattern, require_executable);
}

} // namespace dqxclarity
