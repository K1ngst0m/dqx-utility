#pragma once

#include "../IProcessMemory.hpp"
#include <string>
#include <vector>

namespace dqxclarity {

class ProcessMemory : public IProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory() override = default;

    bool AttachProcess(pid_t) override { return false; }
    bool ReadMemory(uintptr_t, void*, size_t) override { return false; }
    bool WriteMemory(uintptr_t, const void*, size_t) override { return false; }
    void DetachProcess() override {}
    bool IsProcessAttached() const override { return false; }
    pid_t GetAttachedPid() const override { return 0; }
    uintptr_t AllocateMemory(size_t, bool) override { return 0; }
    bool FreeMemory(uintptr_t, size_t) override { return false; }
    bool SetMemoryProtection(uintptr_t, size_t, MemoryProtectionFlags) override { return false; }

    bool ReadString(uintptr_t, std::string&, size_t) override { return false; }
    bool WriteString(uintptr_t, const std::string&) override { return false; }
    uintptr_t GetModuleBaseAddress(const std::string& = "") override { return 0; }
    int ReadInt32(uintptr_t) override { return 0; }
    uint64_t ReadInt64(uintptr_t) override { return 0; }
    uintptr_t GetPointerAddress(uintptr_t, const std::vector<uintptr_t>&) override { return 0; }
    void FlushInstructionCache(uintptr_t, size_t) override {}
};

} // namespace dqxclarity
