#pragma once

#include "../IProcessMemory.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace dqxclarity
{

class ProcessMemory : public IProcessMemory
{
public:
    ProcessMemory();
    ~ProcessMemory() override;

    bool AttachProcess(pid_t pid) override;
    bool ReadMemory(uintptr_t address, void* buffer, size_t size) override;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) override;
    void DetachProcess() override;
    bool IsProcessAttached() const override;
    pid_t GetAttachedPid() const override;
    uintptr_t AllocateMemory(size_t size, bool executable = true) override;
    bool FreeMemory(uintptr_t address, size_t size) override;
    bool SetMemoryProtection(uintptr_t address, size_t size, MemoryProtectionFlags protection) override;

    bool ReadString(uintptr_t address, std::string& output, size_t max_length = 1024) override;
    bool WriteString(uintptr_t address, const std::string& text) override;
    uintptr_t GetModuleBaseAddress(const std::string& module_name = "") override;
    int ReadInt32(uintptr_t address) override;
    uint64_t ReadInt64(uintptr_t address) override;
    uintptr_t GetPointerAddress(uintptr_t base, const std::vector<uintptr_t>& offsets) override;
    void FlushInstructionCache(uintptr_t address, size_t size) override;

private:
    HANDLE m_process_handle;
    DWORD m_process_id;
    bool m_is_attached;

    DWORD ConvertProtectionFlags(MemoryProtectionFlags flags);
};

} // namespace dqxclarity
