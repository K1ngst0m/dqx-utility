#include "PatternScanner.hpp"

#include "../util/Profile.hpp"

namespace dqxclarity
{

PatternScanner::PatternScanner(std::shared_ptr<IProcessMemory> memory)
    : m_memory(memory)
{
}

std::vector<size_t> PatternScanner::BuildBadCharTable(const Pattern& pattern)
{
    std::vector<size_t> table(256, pattern.Size());

    for (size_t i = 0; i < pattern.Size() - 1; ++i)
    {
        if (pattern.mask[i])
        {
            table[pattern.bytes[i]] = pattern.Size() - 1 - i;
        }
    }

    return table;
}

std::optional<size_t> PatternScanner::FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size,
                                                          const Pattern& pattern,
                                                          const std::vector<size_t>& bad_char_table)
{
    if (buffer_size < pattern.Size())
    {
        return std::nullopt;
    }

    size_t i = 0;
    while (i <= buffer_size - pattern.Size())
    {
        size_t j = pattern.Size();

        while (j > 0)
        {
            --j;
            if (!pattern.mask[j] || buffer[i + j] == pattern.bytes[j])
            {
                if (j == 0)
                {
                    return i;
                }
            }
            else
            {
                break;
            }
        }

        uint8_t bad_char = buffer[i + pattern.Size() - 1];
        i += bad_char_table[bad_char];
    }

    return std::nullopt;
}

std::vector<size_t> PatternScanner::FindPatternInBufferAll(const uint8_t* buffer, size_t buffer_size,
                                                           const Pattern& pattern)
{
    std::vector<size_t> results;

    if (buffer_size < pattern.Size())
    {
        return results;
    }

    for (size_t i = 0; i <= buffer_size - pattern.Size(); ++i)
    {
        bool match = true;
        for (size_t j = 0; j < pattern.Size(); ++j)
        {
            if (pattern.mask[j] && buffer[i + j] != pattern.bytes[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            results.push_back(i);
        }
    }

    return results;
}

std::optional<uintptr_t> PatternScanner::ScanRegion(const MemoryRegion& region, const Pattern& pattern)
{
    PROFILE_SCOPE_FUNCTION();
    if (!pattern.IsValid() || region.Size() < pattern.Size())
    {
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(region.Size());
    {
        PROFILE_SCOPE_CUSTOM("ScanRegion.ReadMemory");
        if (!m_memory->ReadMemory(region.start, buffer.data(), buffer.size()))
        {
            return std::nullopt;
        }
    }

    bool has_wildcards = false;
    {
        PROFILE_SCOPE_CUSTOM("ScanRegion.CheckWildcards");
        has_wildcards = std::find(pattern.mask.begin(), pattern.mask.end(), false) != pattern.mask.end();
    }

    if (has_wildcards)
    {
        PROFILE_SCOPE_CUSTOM("ScanRegion.NaiveScan");
        auto all = FindPatternInBufferAll(buffer.data(), buffer.size(), pattern);
        if (!all.empty())
        {
            return region.start + all.front();
        }
        return std::nullopt;
    }

    std::vector<size_t> bad_char_table;
    {
        PROFILE_SCOPE_CUSTOM("ScanRegion.BuildBadCharTable");
        bad_char_table = BuildBadCharTable(pattern);
    }

    std::optional<size_t> offset;
    {
        PROFILE_SCOPE_CUSTOM("ScanRegion.BMHSearch");
        offset = FindPatternInBuffer(buffer.data(), buffer.size(), pattern, bad_char_table);
    }

    if (offset)
    {
        return region.start + *offset;
    }

    return std::nullopt;
}

std::vector<uintptr_t> PatternScanner::ScanRegionAll(const MemoryRegion& region, const Pattern& pattern)
{
    std::vector<uintptr_t> results;

    if (!pattern.IsValid() || region.Size() < pattern.Size())
    {
        return results;
    }

    std::vector<uint8_t> buffer(region.Size());
    if (!m_memory->ReadMemory(region.start, buffer.data(), buffer.size()))
    {
        return results;
    }

    auto offsets = FindPatternInBufferAll(buffer.data(), buffer.size(), pattern);

    for (auto offset : offsets)
    {
        results.push_back(region.start + offset);
    }

    return results;
}

std::optional<uintptr_t> PatternScanner::ScanProcess(const Pattern& pattern, bool require_executable)
{
    PROFILE_SCOPE_FUNCTION();
    if (!m_memory->IsProcessAttached())
    {
        return std::nullopt;
    }

    std::vector<MemoryRegion> regions;
    size_t total_regions = 0;
    {
        PROFILE_SCOPE_CUSTOM("ScanProcess.ParseRegions");
        regions = MemoryRegionParser::ParseMapsFiltered(m_memory->GetAttachedPid(), true, require_executable);
        total_regions = regions.size();
    }

    for (const auto& region : regions)
    {
        PROFILE_SCOPE_CUSTOM("ScanProcess.RegionIteration");
        auto result = ScanRegion(region, pattern);
        if (result)
        {
            return result;
        }
    }

    return std::nullopt;
}

std::optional<uintptr_t> PatternScanner::ScanModule(const Pattern& pattern, const std::string& module_name)
{
    PROFILE_SCOPE_FUNCTION();
    if (!m_memory->IsProcessAttached())
    {
        return std::nullopt;
    }

    std::vector<MemoryRegion> regions;
    size_t total_regions = 0;
    size_t matched_regions = 0;
    {
        PROFILE_SCOPE_CUSTOM("ScanModule.ParseMaps");
        regions = MemoryRegionParser::ParseMaps(m_memory->GetAttachedPid());
        total_regions = regions.size();
    }

    for (const auto& region : regions)
    {
        if (region.pathname.find(module_name) == std::string::npos)
        {
            continue;
        }

        if (!region.IsReadable())
        {
            continue;
        }

        matched_regions++;
        std::optional<uintptr_t> result;
        {
            PROFILE_SCOPE_CUSTOM("ScanModule.RegionIteration");
            result = ScanRegion(region, pattern);
        }
        if (result)
        {
            return result;
        }
    }

    return std::nullopt;
}

std::vector<uintptr_t> PatternScanner::ScanProcessAll(const Pattern& pattern, bool require_executable)
{
    std::vector<uintptr_t> all_results;

    if (!m_memory->IsProcessAttached())
    {
        return all_results;
    }

    auto regions = MemoryRegionParser::ParseMapsFiltered(m_memory->GetAttachedPid(), true, require_executable);

    for (const auto& region : regions)
    {
        auto results = ScanRegionAll(region, pattern);
        all_results.insert(all_results.end(), results.begin(), results.end());
    }

    return all_results;
}

} // namespace dqxclarity
