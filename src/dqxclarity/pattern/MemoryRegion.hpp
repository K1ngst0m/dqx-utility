#pragma once

#include <cstdint>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
using pid_t = DWORD;
#else
#include <sys/types.h>
#endif

namespace dqxclarity
{

enum class MemoryProtection
{
    Read = 1,
    Write = 2,
    Execute = 4
};

struct MemoryRegion
{
    uintptr_t start;
    uintptr_t end;
    int protection;
    std::string pathname;

    size_t Size() const { return end - start; }

    bool IsReadable() const { return protection & static_cast<int>(MemoryProtection::Read); }

    bool IsExecutable() const { return protection & static_cast<int>(MemoryProtection::Execute); }

    bool IsWritable() const { return protection & static_cast<int>(MemoryProtection::Write); }
};

class MemoryRegionParser
{
public:
    static std::vector<MemoryRegion> ParseMaps(pid_t pid);
    static std::vector<MemoryRegion> ParseMapsFiltered(pid_t pid, bool require_readable, bool require_executable);
};

} // namespace dqxclarity
