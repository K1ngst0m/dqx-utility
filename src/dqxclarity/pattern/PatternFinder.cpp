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

    // Fallback: manual chunk scan from module base
    if (!memory_ || !memory_->IsProcessAttached()) return std::nullopt;
    uintptr_t base = memory_->GetModuleBaseAddress(module_name);
    if (base == 0) return std::nullopt;

    const size_t chunk_size = 64 * 1024;
    std::vector<uint8_t> buffer(chunk_size);

    for (uintptr_t addr = base; addr < base + scan_size_bytes; addr += chunk_size) {
        if (!memory_->ReadMemory(addr, buffer.data(), buffer.size())) continue;
        for (size_t i = 0; i + pattern.Size() <= buffer.size(); ++i) {
            if (MatchAt(buffer, i, pattern)) {
                return addr + i;
            }
        }
    }
    return std::nullopt;
}

} // namespace dqxclarity