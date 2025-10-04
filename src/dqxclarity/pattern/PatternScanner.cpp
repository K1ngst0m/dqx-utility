#include "PatternScanner.hpp"
#include <algorithm>
#include <cstring>

namespace dqxclarity {

PatternScanner::PatternScanner(std::shared_ptr<IProcessMemory> memory)
    : m_memory(memory)
{
}

std::vector<size_t> PatternScanner::BuildBadCharTable(const Pattern& pattern) {
    std::vector<size_t> table(256, pattern.Size());

    for (size_t i = 0; i < pattern.Size() - 1; ++i) {
        if (pattern.mask[i]) {
            table[pattern.bytes[i]] = pattern.Size() - 1 - i;
        }
    }

    return table;
}

std::optional<size_t> PatternScanner::FindPatternInBuffer(
    const uint8_t* buffer,
    size_t buffer_size,
    const Pattern& pattern,
    const std::vector<size_t>& bad_char_table)
{
    if (buffer_size < pattern.Size()) {
        return std::nullopt;
    }

    size_t i = 0;
    while (i <= buffer_size - pattern.Size()) {
        size_t j = pattern.Size();

        while (j > 0) {
            --j;
            if (!pattern.mask[j] || buffer[i + j] == pattern.bytes[j]) {
                if (j == 0) {
                    return i;
                }
            } else {
                break;
            }
        }

        uint8_t bad_char = buffer[i + pattern.Size() - 1];
        i += bad_char_table[bad_char];
    }

    return std::nullopt;
}

std::vector<size_t> PatternScanner::FindPatternInBufferAll(
    const uint8_t* buffer,
    size_t buffer_size,
    const Pattern& pattern)
{
    std::vector<size_t> results;

    if (buffer_size < pattern.Size()) {
        return results;
    }

    for (size_t i = 0; i <= buffer_size - pattern.Size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern.Size(); ++j) {
            if (pattern.mask[j] && buffer[i + j] != pattern.bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            results.push_back(i);
        }
    }

    return results;
}

std::optional<uintptr_t> PatternScanner::ScanRegion(
    const MemoryRegion& region,
    const Pattern& pattern)
{
    if (!pattern.IsValid() || region.Size() < pattern.Size()) {
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(region.Size());
    if (!m_memory->ReadMemory(region.start, buffer.data(), buffer.size())) {
        return std::nullopt;
    }

    auto bad_char_table = BuildBadCharTable(pattern);
    auto offset = FindPatternInBuffer(buffer.data(), buffer.size(), pattern, bad_char_table);

    if (offset) {
        return region.start + *offset;
    }

    return std::nullopt;
}

std::vector<uintptr_t> PatternScanner::ScanRegionAll(
    const MemoryRegion& region,
    const Pattern& pattern)
{
    std::vector<uintptr_t> results;

    if (!pattern.IsValid() || region.Size() < pattern.Size()) {
        return results;
    }

    std::vector<uint8_t> buffer(region.Size());
    if (!m_memory->ReadMemory(region.start, buffer.data(), buffer.size())) {
        return results;
    }

    auto offsets = FindPatternInBufferAll(buffer.data(), buffer.size(), pattern);

    for (auto offset : offsets) {
        results.push_back(region.start + offset);
    }

    return results;
}

std::optional<uintptr_t> PatternScanner::ScanProcess(
    const Pattern& pattern,
    bool require_executable)
{
    if (!m_memory->IsProcessAttached()) {
        return std::nullopt;
    }

    auto regions = MemoryRegionParser::ParseMapsFiltered(
        m_memory->GetAttachedPid(),
        true,
        require_executable
    );

    for (const auto& region : regions) {
        auto result = ScanRegion(region, pattern);
        if (result) {
            return result;
        }
    }

    return std::nullopt;
}

std::optional<uintptr_t> PatternScanner::ScanModule(
    const Pattern& pattern,
    const std::string& module_name)
{
    if (!m_memory->IsProcessAttached()) {
        return std::nullopt;
    }

    auto regions = MemoryRegionParser::ParseMaps(m_memory->GetAttachedPid());

    for (const auto& region : regions) {
        if (region.pathname.find(module_name) == std::string::npos) {
            continue;
        }

        if (!region.IsReadable()) {
            continue;
        }

        auto result = ScanRegion(region, pattern);
        if (result) {
            return result;
        }
    }

    return std::nullopt;
}

std::vector<uintptr_t> PatternScanner::ScanProcessAll(
    const Pattern& pattern,
    bool require_executable)
{
    std::vector<uintptr_t> all_results;

    if (!m_memory->IsProcessAttached()) {
        return all_results;
    }

    auto regions = MemoryRegionParser::ParseMapsFiltered(
        m_memory->GetAttachedPid(),
        true,
        require_executable
    );

    for (const auto& region : regions) {
        auto results = ScanRegionAll(region, pattern);
        all_results.insert(all_results.end(), results.begin(), results.end());
    }

    return all_results;
}

} // namespace dqxclarity
