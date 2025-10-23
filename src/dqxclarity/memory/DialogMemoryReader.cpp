#include "DialogMemoryReader.hpp"
#include "../pattern/Pattern.hpp"
#include "../util/Profile.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>

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

DialogMemoryReader::DialogMemoryReader(std::shared_ptr<IProcessMemory> memory)
    : memory_(memory)
{
}

bool DialogMemoryReader::Initialize()
{
    PROFILE_SCOPE_FUNCTION();

    if (!memory_ || !memory_->IsProcessAttached())
    {
        if (logger_.error)
            logger_.error("DialogMemoryReader: Memory interface not attached");
        return false;
    }

    if (verbose_)
        std::cout << "DialogMemoryReader: Initializing...\n";

    // Try to find the pattern on initialization (optional - will retry during polling if needed)
    uintptr_t pattern_addr = FindDialogPattern();
    if (pattern_addr != 0)
    {
        if (verbose_)
        {
            std::cout << "DialogMemoryReader: Pattern found at 0x" << std::hex << pattern_addr << std::dec << "\n";
        }
        if (logger_.info)
            logger_.info("DialogMemoryReader: Initialized successfully");
        initialized_ = true;
        return true;
    }
    else
    {
        if (verbose_)
            std::cout << "DialogMemoryReader: Pattern not found during init (will retry during polling)\n";
        if (logger_.warn)
            logger_.warn("DialogMemoryReader: Pattern not found during init");
        // Still mark as initialized - we'll try to find it during polling
        initialized_ = true;
        return true;
    }
}

bool DialogMemoryReader::PollDialogData()
{
    PROFILE_SCOPE_FUNCTION();

    if (!initialized_ || !memory_ || !memory_->IsProcessAttached())
    {
        return false;
    }

    try
    {
        auto now = std::chrono::steady_clock::now();

        // Step 1: Find the dialog pattern (fast path: cached region, slow path: full scan)
        uintptr_t pattern_addr = FindDialogPattern();
        if (pattern_addr == 0)
        {
            // No dialog active or pattern not found
            return false;
        }

        // Step 2: Read the pointer at pattern_addr + kPointerOffset
        uintptr_t dialog_base_addr = pattern_addr + kPointerOffset;
        uint32_t dialog_actual_addr = 0;

        if (!memory_->ReadMemory(dialog_base_addr, &dialog_actual_addr, sizeof(dialog_actual_addr)))
        {
            if (verbose_)
                std::cout << "DialogMemoryReader: Failed to read pointer at 0x" << std::hex << dialog_base_addr
                          << std::dec << "\n";
            return false;
        }

        // Step 3: Check if pointer is null (no active dialog)
        if (dialog_actual_addr == 0)
        {
            return false;
        }

        // Step 4: Extract the null-terminated dialog text
        std::string text;
        if (!ExtractNullTerminatedString(static_cast<uintptr_t>(dialog_actual_addr), text))
        {
            if (verbose_)
                std::cout << "DialogMemoryReader: Failed to read dialog text at 0x" << std::hex << dialog_actual_addr
                          << std::dec << "\n";
            return false;
        }

        // Step 5: Check if text is new (avoid duplicates)
        if (text == last_dialog_text_)
        {
            return false; // Same text as before
        }

        // Step 6: Handle empty text (dialog was cleared)
        if (text.empty())
        {
            return false;
        }

        // Step 7: Update captured text and timestamp
        last_dialog_text_ = text;
        last_dialog_time_ = now;
        // Note: NPC name extraction not available in this mode (no register context)
        last_npc_name_ = "No_NPC";

        if (verbose_)
        {
            std::cout << "DialogMemoryReader: Captured text: " << text.substr(0, 50)
                      << (text.length() > 50 ? "..." : "") << "\n";
        }

        return true;
    }
    catch (...)
    {
        // Silently ignore errors to avoid crashes
        return false;
    }
}

uintptr_t DialogMemoryReader::FindDialogPattern()
{
    PROFILE_SCOPE_FUNCTION();

    // Fast path: Check cached region first
    if (last_pattern_addr_ != 0 && last_region_base_ != 0 && last_region_size_ > 0)
    {
        PROFILE_SCOPE_CUSTOM("DialogMemoryReader.FastPath");
        uintptr_t addr = ScanRegionForPattern(last_region_base_, last_region_size_);
        if (addr != 0)
        {
            last_pattern_addr_ = addr;
            return addr;
        }
        // Cache miss - pattern moved or disappeared
    }

    // Slow path: Full scan of non-executable memory
    {
        PROFILE_SCOPE_CUSTOM("DialogMemoryReader.SlowPath");
        uintptr_t addr = ScanAllNonExecutableMemory();
        if (addr != 0)
        {
            last_pattern_addr_ = addr;
            return addr;
        }
    }

    // Pattern not found
    last_pattern_addr_ = 0;
    return 0;
}

uintptr_t DialogMemoryReader::ScanRegionForPattern(uintptr_t base_address, size_t size)
{
    if (size == 0 || size > 100 * 1024 * 1024) // Sanity check: max 100MB
        return 0;

    std::vector<uint8_t> buffer(size);
    if (!memory_->ReadMemory(base_address, buffer.data(), size))
    {
        return 0;
    }

    size_t offset = FindPatternInBuffer(buffer.data(), size);
    if (offset != SIZE_MAX)
    {
        return base_address + offset;
    }

    return 0;
}

uintptr_t DialogMemoryReader::ScanAllNonExecutableMemory()
{
    PROFILE_SCOPE_FUNCTION();

    auto regions = GetNonExecutableRegions();

    if (verbose_)
        std::cout << "DialogMemoryReader: Scanning " << regions.size() << " non-executable regions\n";

    for (const auto& region : regions)
    {
        uintptr_t addr = ScanRegionForPattern(region.start, region.Size());
        if (addr != 0)
        {
            // Cache this region for fast path next time
            last_region_base_ = region.start;
            last_region_size_ = region.Size();

            if (verbose_)
            {
                std::cout << "DialogMemoryReader: Pattern found in region 0x" << std::hex << region.start << " - 0x"
                          << region.end << std::dec << "\n";
            }

            return addr;
        }
    }

    // Pattern not found in any region
    if (verbose_)
        std::cout << "DialogMemoryReader: Pattern not found in any non-executable region\n";

    return 0;
}

std::vector<MemoryRegion> DialogMemoryReader::GetNonExecutableRegions()
{
    std::vector<MemoryRegion> regions;

#ifdef _WIN32
    // Windows implementation: enumerate memory regions via VirtualQueryEx
    pid_t pid = memory_->GetAttachedPid();
    HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process_handle)
        return regions;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;
    const uintptr_t max_address = 0x7FFFFFFF; // 32-bit process limit

    while (address < max_address)
    {
        SIZE_T result = VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi));
        if (result == 0)
            break;

        // Filter: committed, readable, not executable, not MEM_IMAGE
        if (mbi.State == MEM_COMMIT && mbi.Type != MEM_IMAGE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY))
        {
            MemoryRegion region;
            region.start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            region.end = region.start + mbi.RegionSize;
            // Set protection flags: readable, optionally writable, not executable
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
    // Linux implementation: parse /proc/[pid]/maps
    pid_t pid = memory_->GetAttachedPid();
    std::ifstream maps_file("/proc/" + std::to_string(pid) + "/maps");
    if (!maps_file.is_open())
        return regions;

    std::string line;
    while (std::getline(maps_file, line))
    {
        std::istringstream iss(line);
        std::string address_range, perms;
        iss >> address_range >> perms;

        // Parse permissions: should be readable, not executable
        if (perms.length() < 3)
            continue;
        bool readable = (perms[0] == 'r');
        bool writable = (perms[1] == 'w');
        bool executable = (perms[2] == 'x');

        if (!readable || executable)
            continue;

        // Parse address range
        size_t dash_pos = address_range.find('-');
        if (dash_pos == std::string::npos)
            continue;

        uintptr_t start_addr = std::stoull(address_range.substr(0, dash_pos), nullptr, 16);
        uintptr_t end_addr = std::stoull(address_range.substr(dash_pos + 1), nullptr, 16);

        MemoryRegion region;
        region.start = start_addr;
        region.end = end_addr;
        // Set protection flags
        region.protection = 0;
        if (readable)
            region.protection |= static_cast<int>(MemoryProtection::Read);
        if (writable)
            region.protection |= static_cast<int>(MemoryProtection::Write);
        // executable is false (already filtered above)
        regions.push_back(region);
    }
#endif

    return regions;
}

size_t DialogMemoryReader::FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size)
{
    if (buffer_size < kPatternSize)
        return SIZE_MAX;

    // Simple Boyer-Moore-like search with wildcard support
    for (size_t i = 0; i <= buffer_size - kPatternSize; ++i)
    {
        bool match = true;
        for (size_t j = 0; j < kPatternSize; ++j)
        {
            // Check for wildcard (0xFF in pattern means any byte matches)
            // Special case: byte 17 (index 17) is wildcard
            if (j == 17)
                continue; // Wildcard - skip comparison

            if (buffer[i + j] != kDialogPattern[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return i;
        }
    }

    return SIZE_MAX;
}

bool DialogMemoryReader::ExtractNullTerminatedString(uintptr_t address, std::string& output, size_t max_length)
{
    output.clear();

    if (address == 0)
        return false;

    // Read string character by character until null terminator
    std::vector<uint8_t> text_bytes;
    text_bytes.reserve(256); // Pre-allocate for typical dialog length

    uintptr_t curr_addr = address;
    for (size_t i = 0; i < max_length; ++i)
    {
        uint8_t byte = 0;
        if (!memory_->ReadMemory(curr_addr, &byte, 1))
        {
            // Read error - return what we have so far
            break;
        }

        if (byte == 0)
        {
            // Null terminator found - end of string
            break;
        }

        text_bytes.push_back(byte);
        ++curr_addr;
    }

    if (text_bytes.empty())
        return false;

    // Convert bytes to string (assume UTF-8 or Shift-JIS encoding)
    output.assign(text_bytes.begin(), text_bytes.end());
    return true;
}

} // namespace dqxclarity
