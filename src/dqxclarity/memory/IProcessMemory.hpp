#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
    using pid_t = DWORD;
#else
    #include <sys/types.h>
#endif

namespace dqxclarity {

enum class MemoryProtectionFlags {
    Read = 1,
    Write = 2,
    Execute = 4,
    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    ReadWriteExecute = Read | Write | Execute
};

class IProcessMemory {
public:
    virtual ~IProcessMemory() = default;

    virtual bool AttachProcess(pid_t pid) = 0;

    virtual bool ReadMemory(uintptr_t address, void* buffer, size_t size) = 0;

    virtual bool WriteMemory(uintptr_t address, const void* buffer, size_t size) = 0;

    virtual void DetachProcess() = 0;

    virtual bool IsProcessAttached() const = 0;

    virtual pid_t GetAttachedPid() const = 0;

    virtual uintptr_t AllocateMemory(size_t size, bool executable = true) = 0;

    virtual bool FreeMemory(uintptr_t address, size_t size) = 0;

    virtual bool SetMemoryProtection(uintptr_t address, size_t size, MemoryProtectionFlags protection) = 0;

    // Extended helpers required by hook logic
    virtual bool ReadString(uintptr_t address, std::string& output, size_t max_length = 1024) = 0;
    virtual bool WriteString(uintptr_t address, const std::string& text) = 0;
    virtual uintptr_t GetModuleBaseAddress(const std::string& module_name = "") = 0;
    virtual int ReadInt32(uintptr_t address) = 0;
    virtual uint64_t ReadInt64(uintptr_t address) = 0;
    virtual uintptr_t GetPointerAddress(uintptr_t base, const std::vector<uintptr_t>& offsets) = 0;
    virtual void FlushInstructionCache(uintptr_t address, size_t size) = 0;
};

} // namespace dqxclarity