#include "MemoryRegion.hpp"
#include <libmem/libmem.hpp>

namespace dqxclarity
{

namespace
{
int ToInternalProtection(libmem::Prot prot)
{
    switch (prot)
    {
    case libmem::Prot::R:
        return static_cast<int>(MemoryProtection::Read);
    case libmem::Prot::W:
        return static_cast<int>(MemoryProtection::Write);
    case libmem::Prot::X:
        return static_cast<int>(MemoryProtection::Execute);
    case libmem::Prot::XR:
        return static_cast<int>(MemoryProtection::Execute) | static_cast<int>(MemoryProtection::Read);
    case libmem::Prot::XW:
        return static_cast<int>(MemoryProtection::Execute) | static_cast<int>(MemoryProtection::Write);
    case libmem::Prot::RW:
        return static_cast<int>(MemoryProtection::Read) | static_cast<int>(MemoryProtection::Write);
    case libmem::Prot::XRW:
        return static_cast<int>(MemoryProtection::Read) | static_cast<int>(MemoryProtection::Write) |
               static_cast<int>(MemoryProtection::Execute);
    default:
        return 0;
    }
}

std::vector<MemoryRegion> ParseMapsFilteredInternal(libmem::Pid pid, bool require_readable, bool require_executable)
{
    std::vector<MemoryRegion> regions;

    auto proc = libmem::GetProcess(pid);
    if (!proc)
    {
        return regions;
    }

    auto modules = libmem::EnumModules(&*proc);

    auto segments = libmem::EnumSegments(&*proc);
    if (!segments)
    {
        return regions;
    }

    for (const auto& segment : *segments)
    {
        MemoryRegion region;
        region.start = segment.base;
        region.end = segment.end;
        region.protection = ToInternalProtection(segment.prot);

        if (modules)
        {
            for (const auto& module : *modules)
            {
                if (segment.base >= module.base && segment.base < module.end)
                {
                    region.pathname = module.path;
                    break;
                }
            }
        }

        if (require_readable && !region.IsReadable())
        {
            continue;
        }
        if (require_executable && !region.IsExecutable())
        {
            continue;
        }

        regions.push_back(region);
    }

    return regions;
}
} // namespace

std::vector<MemoryRegion> MemoryRegionParser::ParseMaps(pid_t pid) { return ParseMapsFiltered(pid, false, false); }

std::vector<MemoryRegion> MemoryRegionParser::ParseMapsFiltered(pid_t pid, bool require_readable,
                                                                bool require_executable)
{
    return ParseMapsFilteredInternal(static_cast<libmem::Pid>(pid), require_readable, require_executable);
}

} // namespace dqxclarity
