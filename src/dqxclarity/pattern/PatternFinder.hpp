#pragma once

#include "Pattern.hpp"
#include "../memory/IProcessMemory.hpp"
#include "MemoryRegion.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

namespace dqxclarity
{

class PatternFinder
{
public:
    explicit PatternFinder(std::shared_ptr<IProcessMemory> memory)
        : memory_(std::move(memory))
    {
    }

    // Scan a specific module's mapped regions
    std::optional<uintptr_t> FindInModule(const Pattern& pattern, const std::string& module_name);

    // Optimized: scan with pre-parsed regions (avoids repeated ParseMaps calls)
    std::optional<uintptr_t> FindInModuleWithRegions(const Pattern& pattern, const std::string& module_name,
                                                     const std::vector<MemoryRegion>& regions);

    // Scan all process regions that are executable
    std::optional<uintptr_t> FindInProcessExec(const Pattern& pattern);

    // Try module/exec first, then fall back to chunk scanning the first scan_size bytes from module base
    std::optional<uintptr_t> FindWithFallback(const Pattern& pattern, const std::string& module_name,
                                              size_t scan_size_bytes = 80u * 1024u * 1024u);

    // Diagnostics: naive scan across all readable regions belonging to the module (case-insensitive pathname match)
    std::vector<uintptr_t> FindAllInModuleNaive(const Pattern& pattern, const std::string& module_name)
    {
        std::vector<uintptr_t> results;
        if (!memory_ || !memory_->IsProcessAttached())
            return results;
        auto regions = MemoryRegionParser::ParseMaps(memory_->GetAttachedPid());
        std::string mod = module_name;
        std::transform(mod.begin(), mod.end(), mod.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        for (const auto& r : regions)
        {
            std::string path = r.pathname;
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            if (path.find(mod) == std::string::npos)
                continue;
            if (!r.IsReadable())
                continue;
            size_t size = r.Size();
            if (size < pattern.Size())
                continue;
            std::vector<uint8_t> buf(size);
            if (!memory_->ReadMemory(r.start, buf.data(), buf.size()))
                continue;
            for (size_t i = 0; i + pattern.Size() <= buf.size(); ++i)
            {
                if (MatchAt(buf, i, pattern))
                    results.push_back(r.start + i);
            }
        }
        return results;
    }

private:
    std::shared_ptr<IProcessMemory> memory_;

    static bool MatchAt(const std::vector<uint8_t>& buf, size_t i, const Pattern& pat)
    {
        if (i + pat.Size() > buf.size())
            return false;
        for (size_t j = 0; j < pat.Size(); ++j)
        {
            if (!pat.mask[j])
                continue;
            if (buf[i + j] != pat.bytes[j])
                return false;
        }
        return true;
    }
};

} // namespace dqxclarity
