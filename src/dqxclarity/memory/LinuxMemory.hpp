#pragma once

#include "IProcessMemory.hpp"
#include <sys/types.h>

namespace dqxclarity {

class LinuxMemory : public IProcessMemory {
public:
    LinuxMemory();
    ~LinuxMemory() override;

    bool AttachProcess(pid_t pid) override;
    bool ReadMemory(uintptr_t address, void* buffer, size_t size) override;
    bool WriteMemory(uintptr_t address, const void* buffer, size_t size) override;
    void DetachProcess() override;
    bool IsProcessAttached() const override;
    pid_t GetAttachedPid() const override;

private:
    pid_t m_attachedPid;
    bool m_isAttached;

    bool IsProcessValid(pid_t pid) const;
};

} // namespace dqxclarity