#include "../MemoryRegion.hpp"
#include <windows.h>
#include <psapi.h>
#include <vector>

namespace dqxclarity {

std::vector<MemoryRegion> MemoryRegionParser::ParseMapsFiltered(
    pid_t pid,
    bool require_readable,
    bool require_executable)
{
    std::vector<MemoryRegion> regions;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;
    while (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT) {
            MemoryRegion region{};
            region.start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            region.end = region.start + mbi.RegionSize;
            int prot = 0;
            if (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) prot |= static_cast<int>(MemoryProtection::Read);
            if (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) prot |= static_cast<int>(MemoryProtection::Write);
            if (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) prot |= static_cast<int>(MemoryProtection::Execute);
            region.protection = prot;

            char module_name[MAX_PATH] = {0};
            if (GetMappedFileNameA(process, mbi.BaseAddress, module_name, MAX_PATH)) {
                region.pathname = module_name;
            }

            if (require_readable && !region.IsReadable()) { address += mbi.RegionSize; continue; }
            if (require_executable && !region.IsExecutable()) { address += mbi.RegionSize; continue; }
            regions.push_back(region);
        }
        address += mbi.RegionSize;
        if (address == 0) break;
    }
    CloseHandle(process);
    return regions;
}

std::vector<MemoryRegion> MemoryRegionParser::ParseMaps(pid_t pid) {
    return ParseMapsFiltered(pid, false, false);
}

} // namespace dqxclarity
