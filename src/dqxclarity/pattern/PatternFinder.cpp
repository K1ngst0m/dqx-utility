#include "PatternFinder.hpp"
#include "PatternScanner.hpp"
#include <vector>

#include "../util/Profile.hpp"

namespace dqxclarity
{

std::optional<uintptr_t> PatternFinder::FindInModule(const Pattern& pattern, const std::string& module_name)
{
    PROFILE_SCOPE_FUNCTION();
    if (!memory_ || !memory_->IsProcessAttached())
        return std::nullopt;
    PatternScanner scanner(memory_);
    return scanner.ScanModule(pattern, module_name);
}

std::optional<uintptr_t> PatternFinder::FindInModuleWithRegions(const Pattern& pattern, const std::string& module_name,
                                                                const std::vector<MemoryRegion>& regions)
{
    PROFILE_SCOPE_FUNCTION();
    if (!memory_ || !memory_->IsProcessAttached())
        return std::nullopt;
    PatternScanner scanner(memory_);
    return scanner.ScanModuleWithRegions(pattern, module_name, regions);
}

std::optional<uintptr_t> PatternFinder::FindInProcessExec(const Pattern& pattern)
{
    PROFILE_SCOPE_FUNCTION();
    if (!memory_ || !memory_->IsProcessAttached())
        return std::nullopt;
    PatternScanner scanner(memory_);
    return scanner.ScanProcess(pattern, /*require_executable=*/true);
}

std::optional<uintptr_t> PatternFinder::FindWithFallback(const Pattern& pattern, const std::string& module_name,
                                                         size_t scan_size_bytes)
{
    PROFILE_SCOPE_FUNCTION();
    if (auto m = FindInModule(pattern, module_name))
        return m;
    if (auto e = FindInProcessExec(pattern))
        return e;

    // Fallback: region-wise scan within [base, base + scan_size_bytes), ignoring pathname
    {
        PROFILE_SCOPE_CUSTOM("PatternFinder::NaiveFallbackScan");
        if (!memory_ || !memory_->IsProcessAttached())
            return std::nullopt;
        uintptr_t base = memory_->GetModuleBaseAddress(module_name);
        if (base == 0)
            return std::nullopt;

        std::vector<MemoryRegion> regions;
        {
            PROFILE_SCOPE_CUSTOM("PatternFinder::Fallback.ParseRegions");
            regions = MemoryRegionParser::ParseMapsFiltered(memory_->GetAttachedPid(), /*require_readable=*/true,
                                                            /*require_executable=*/false);
        }

        size_t total_scan_bytes = 0;
        for (const auto& region : regions)
        {
            // Restrict to the address window relative to the module base
            if (region.end <= base || region.start >= base + scan_size_bytes)
                continue;
            uintptr_t start = (std::max)(region.start, base);
            uintptr_t end = (std::min)(region.end, base + scan_size_bytes);
            size_t size = static_cast<size_t>(end - start);
            if (size < pattern.Size())
                continue;

            PROFILE_SCOPE_CUSTOM("PatternFinder::Fallback.ScanRegion");
            total_scan_bytes += size;

            std::vector<uint8_t> buffer(size);
            {
                PROFILE_SCOPE_CUSTOM("PatternFinder::Fallback.ReadMemory");
                if (!memory_->ReadMemory(start, buffer.data(), buffer.size()))
                    continue;
            }

            {
                PROFILE_SCOPE_CUSTOM("PatternFinder::Fallback.NaiveSearch");
                for (size_t i = 0; i + pattern.Size() <= buffer.size(); ++i)
                {
                    if (MatchAt(buffer, i, pattern))
                    {
                        return start + i;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace dqxclarity