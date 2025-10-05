#pragma once

#include "Pattern.hpp"
#include "../memory/IProcessMemory.hpp"
#include <memory>
#include <optional>
#include <string>

namespace dqxclarity {

class PatternFinder {
public:
    explicit PatternFinder(std::shared_ptr<IProcessMemory> memory)
        : memory_(std::move(memory)) {}

    // Scan a specific module's mapped regions
    std::optional<uintptr_t> FindInModule(const Pattern& pattern, const std::string& module_name);

    // Scan all process regions that are executable
    std::optional<uintptr_t> FindInProcessExec(const Pattern& pattern);

    // Try module/exec first, then fall back to chunk scanning the first scan_size bytes from module base
    std::optional<uintptr_t> FindWithFallback(const Pattern& pattern, const std::string& module_name, size_t scan_size_bytes = 80u * 1024u * 1024u);

private:
    std::shared_ptr<IProcessMemory> memory_;

    static bool MatchAt(const std::vector<uint8_t>& buf, size_t i, const Pattern& pat) {
        if (i + pat.Size() > buf.size()) return false;
        for (size_t j = 0; j < pat.Size(); ++j) {
            if (!pat.mask[j]) continue;
            if (buf[i + j] != pat.bytes[j]) return false;
        }
        return true;
    }
};

} // namespace dqxclarity