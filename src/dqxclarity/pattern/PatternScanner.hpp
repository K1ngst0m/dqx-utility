#pragma once

#include "Pattern.hpp"
#include "MemoryRegion.hpp"
#include "../memory/IProcessMemory.hpp"
#include <memory>
#include <vector>
#include <optional>

namespace dqxclarity
{

class PatternScanner
{
public:
    explicit PatternScanner(std::shared_ptr<IProcessMemory> memory);

    std::optional<uintptr_t> ScanRegion(const MemoryRegion& region, const Pattern& pattern);

    std::vector<uintptr_t> ScanRegionAll(const MemoryRegion& region, const Pattern& pattern);

    std::optional<uintptr_t> ScanProcess(const Pattern& pattern, bool require_executable = true);

    std::optional<uintptr_t> ScanModule(const Pattern& pattern, const std::string& module_name);

    std::optional<uintptr_t> ScanModuleWithRegions(const Pattern& pattern, const std::string& module_name,
                                                   const std::vector<MemoryRegion>& regions);

    std::vector<uintptr_t> ScanProcessAll(const Pattern& pattern, bool require_executable = true);

private:
    std::shared_ptr<IProcessMemory> m_memory;

    std::vector<size_t> BuildBadCharTable(const Pattern& pattern);

    std::optional<size_t> FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size, const Pattern& pattern,
                                              const std::vector<size_t>& bad_char_table);

    std::vector<size_t> FindPatternInBufferAll(const uint8_t* buffer, size_t buffer_size, const Pattern& pattern);
};

} // namespace dqxclarity
