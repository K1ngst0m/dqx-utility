#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace dqxclarity {

class IProcessMemory {
public:
    virtual ~IProcessMemory() = default;

    virtual bool AttachProcess(pid_t pid) = 0;

    virtual bool ReadMemory(uintptr_t address, void* buffer, size_t size) = 0;

    virtual bool WriteMemory(uintptr_t address, const void* buffer, size_t size) = 0;

    virtual void DetachProcess() = 0;

    virtual bool IsProcessAttached() const = 0;

    virtual pid_t GetAttachedPid() const = 0;
};

} // namespace dqxclarity