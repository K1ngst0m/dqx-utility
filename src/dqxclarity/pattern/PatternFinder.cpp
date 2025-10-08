#include "PatternFinder.hpp"
#include "PatternScanner.hpp"
#include <vector>

namespace dqxclarity {

std::optional<uintptr_t> PatternFinder::FindInModule(const Pattern& pattern, const std::string& module_name) {
    if (!memory_ || !memory_->IsProcessAttached()) return std::nullopt;
    PatternScanner scanner(memory_);
    return scanner.ScanModule(pattern, module_name);
}

std::optional<uintptr_t> PatternFinder::FindInProcessExec(const Pattern& pattern) {
    if (!memory_ || !memory_->IsProcessAttached()) return std::nullopt;
    PatternScanner scanner(memory_);
    return scanner.ScanProcess(pattern, /*require_executable=*/true);
}

std::optional<uintptr_t> PatternFinder::FindWithFallback(const Pattern& pattern, const std::string& module_name, size_t scan_size_bytes) {
    if (auto m = FindInModule(pattern, module_name)) return m;
    if (auto e = FindInProcessExec(pattern)) return e;

    // Fallback: region-wise scan within [base, base + scan_size_bytes), ignoring pathname
    if (!memory_ || !memory_->IsProcessAttached()) return std::nullopt;
    uintptr_t base = memory_->GetModuleBaseAddress(module_name);
    if (base == 0) return std::nullopt;

    auto regions = MemoryRegionParser::ParseMapsFiltered(memory_->GetAttachedPid(), /*require_readable=*/true, /*require_executable=*/false);
    for (const auto& region : regions) {
        // Restrict to the address window relative to the module base
        if (region.end <= base || region.start >= base + scan_size_bytes) continue;
        uintptr_t start = (std::max)(region.start, base);
        uintptr_t end   = (std::min)(region.end,   base + scan_size_bytes);
        size_t size = static_cast<size_t>(end - start);
        if (size < pattern.Size()) continue;

        std::vector<uint8_t> buffer(size);
        if (!memory_->ReadMemory(start, buffer.data(), buffer.size())) continue;
        for (size_t i = 0; i + pattern.Size() <= buffer.size(); ++i) {
            if (MatchAt(buffer, i, pattern)) {
                return start + i;
            }
        }
    }
    return std::nullopt;
}

} // namespace dqxclarity