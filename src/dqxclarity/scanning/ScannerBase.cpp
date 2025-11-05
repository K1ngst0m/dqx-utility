#include "ScannerBase.hpp"
#include "../util/Profile.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <sstream>
#endif

namespace dqxclarity
{

ScannerBase::ScannerBase(const ScannerCreateInfo& create_info)
    : memory_(create_info.memory)
    , logger_(create_info.logger)
    , verbose_(create_info.verbose)
    , pattern_(create_info.pattern)
{
}

bool ScannerBase::Initialize()
{
    if (!memory_ || !memory_->IsProcessAttached())
    {
        if (logger_.error)
            logger_.error("ScannerBase: Memory interface not attached");
        return false;
    }

    initialized_ = OnInitialize();
    return initialized_;
}

bool ScannerBase::Poll()
{
    if (!IsActive())
        return false;

    return OnPoll();
}

void ScannerBase::Shutdown()
{
    if (shutdown_)
        return;

    OnShutdown();
    shutdown_ = true;
    initialized_ = false;
}

uintptr_t ScannerBase::FindPattern(const Pattern& pattern, bool require_executable)
{
    if (last_pattern_addr_ != 0 && last_region_base_ != 0 && last_region_size_ > 0)
    {
        PROFILE_SCOPE_CUSTOM("ScannerBase.FastPath");
        uintptr_t addr = ScanRegionForPattern(last_region_base_, last_region_size_, pattern);
        if (addr != 0)
        {
            last_pattern_addr_ = addr;
            return addr;
        }
    }

    PROFILE_SCOPE_CUSTOM("ScannerBase.SlowPath");
    uintptr_t addr = ScanAllMemory(pattern, require_executable);
    if (addr != 0)
    {
        last_pattern_addr_ = addr;
    }
    return addr;
}

uintptr_t ScannerBase::ScanRegionForPattern(uintptr_t base_address, size_t size, const Pattern& pattern)
{
    if (size == 0 || size > 100 * 1024 * 1024)
        return 0;

    std::vector<uint8_t> buffer(size);
    if (!memory_->ReadMemory(base_address, buffer.data(), size))
    {
        return 0;
    }

    size_t offset = FindPatternInBuffer(buffer.data(), size, pattern);
    if (offset != SIZE_MAX)
    {
        return base_address + offset;
    }

    return 0;
}

uintptr_t ScannerBase::ScanAllMemory(const Pattern& pattern, bool require_executable)
{
    PROFILE_SCOPE_FUNCTION();

    auto regions = require_executable ? GetExecutableRegions() : GetNonExecutableRegions();

    if (verbose_)
        std::cout << "ScannerBase: Scanning " << regions.size() << " regions\n";

    for (const auto& region : regions)
    {
        uintptr_t addr = ScanRegionForPattern(region.start, region.Size(), pattern);
        if (addr != 0)
        {
            last_region_base_ = region.start;
            last_region_size_ = region.Size();

            if (verbose_)
            {
                std::cout << "ScannerBase: Pattern found in region 0x" << std::hex << region.start << " - 0x"
                          << region.end << std::dec << "\n";
            }

            return addr;
        }
    }

    if (verbose_)
        std::cout << "ScannerBase: Pattern not found\n";

    return 0;
}

std::vector<MemoryRegion> ScannerBase::GetNonExecutableRegions()
{
    std::vector<MemoryRegion> regions;

#ifdef _WIN32
    pid_t pid = memory_->GetAttachedPid();
    HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process_handle)
        return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;
    const uintptr_t max_address = 0x7FFFFFFF;

    while (address < max_address)
    {
        SIZE_T result = VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (result == 0)
            break;

        if (mbi.State == MEM_COMMIT && mbi.Type != MEM_IMAGE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY))
        {
            MemoryRegion region;
            region.start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            region.end = region.start + mbi.RegionSize;
            region.protection = static_cast<int>(MemoryProtection::Read);
            if (mbi.Protect == PAGE_READWRITE)
            {
                region.protection |= static_cast<int>(MemoryProtection::Write);
            }
            regions.push_back(region);
        }

        address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    CloseHandle(process_handle);
#else
    pid_t pid = memory_->GetAttachedPid();
    std::ifstream maps_file("/proc/" + std::to_string(pid) + "/maps");
    if (!maps_file.is_open())
        return regions;

    std::string line;
    while (std::getline(maps_file, line))
    {
        std::istringstream iss(line);
        uintptr_t start, end;
        char dash, r, w, x, p;
        if (iss >> std::hex >> start >> dash >> end >> r >> w >> x >> p)
        {
            if (r == 'r' && x != 'x')
            {
                MemoryRegion region;
                region.start = start;
                region.end = end;
                region.protection = static_cast<int>(MemoryProtection::Read);
                if (w == 'w')
                {
                    region.protection |= static_cast<int>(MemoryProtection::Write);
                }
                regions.push_back(region);
            }
        }
    }
#endif

    return regions;
}

std::vector<MemoryRegion> ScannerBase::GetExecutableRegions()
{
    std::vector<MemoryRegion> regions;

#ifdef _WIN32
    pid_t pid = memory_->GetAttachedPid();
    HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process_handle)
        return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;
    const uintptr_t max_address = 0x7FFFFFFF;

    while (address < max_address)
    {
        SIZE_T result = VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (result == 0)
            break;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE &&
            (mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE))
        {
            MemoryRegion region;
            region.start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            region.end = region.start + mbi.RegionSize;
            region.protection = static_cast<int>(MemoryProtection::Read) | static_cast<int>(MemoryProtection::Execute);
            regions.push_back(region);
        }

        address = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    CloseHandle(process_handle);
#else
    pid_t pid = memory_->GetAttachedPid();
    std::ifstream maps_file("/proc/" + std::to_string(pid) + "/maps");
    if (!maps_file.is_open())
        return regions;

    std::string line;
    while (std::getline(maps_file, line))
    {
        std::istringstream iss(line);
        uintptr_t start, end;
        char dash, r, w, x, p;
        if (iss >> std::hex >> start >> dash >> end >> r >> w >> x >> p)
        {
            if (r == 'r' && x == 'x')
            {
                MemoryRegion region;
                region.start = start;
                region.end = end;
                region.protection = static_cast<int>(MemoryProtection::Read) | static_cast<int>(MemoryProtection::Execute);
                regions.push_back(region);
            }
        }
    }
#endif

    return regions;
}

bool ScannerBase::ReadString(uintptr_t address, std::string& output, size_t max_length)
{
    output.clear();

    if (address == 0 || max_length == 0)
        return false;

    std::vector<uint8_t> buffer(max_length);
    if (!memory_->ReadMemory(address, buffer.data(), max_length))
        return false;

    size_t length = 0;
    while (length < max_length && buffer[length] != '\0')
    {
        length++;
    }

    if (length > 0)
    {
        output.assign(reinterpret_cast<const char*>(buffer.data()), length);
        return true;
    }

    return false;
}

size_t ScannerBase::FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size, const Pattern& pattern)
{
    if (!buffer || buffer_size == 0 || pattern.bytes.empty())
        return SIZE_MAX;

    const size_t pattern_size = pattern.bytes.size();
    if (buffer_size < pattern_size)
        return SIZE_MAX;

    for (size_t i = 0; i <= buffer_size - pattern_size; ++i)
    {
        bool match = true;
        for (size_t j = 0; j < pattern_size; ++j)
        {
            if (!pattern.mask[j])
                continue;

            if (buffer[i + j] != pattern.bytes[j])
            {
                match = false;
                break;
            }
        }

        if (match)
            return i;
    }

    return SIZE_MAX;
}

} // namespace dqxclarity

