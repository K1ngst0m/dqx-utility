#include "LinuxMemory.hpp"
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <fstream>
#include <string>

namespace dqxclarity {

LinuxMemory::LinuxMemory()
    : m_attachedPid(-1), m_isAttached(false) {
}

LinuxMemory::~LinuxMemory() {
    DetachProcess();
}

bool LinuxMemory::AttachProcess(pid_t pid) {
    if (m_isAttached) {
        DetachProcess();
    }

    if (!IsProcessValid(pid)) {
        return false;
    }

    m_attachedPid = pid;
    m_isAttached = true;
    return true;
}

bool LinuxMemory::ReadMemory(uintptr_t address, void* buffer, size_t size) {
    if (!m_isAttached || buffer == nullptr || size == 0) {
        return false;
    }

    struct iovec local_iov;
    local_iov.iov_base = buffer;
    local_iov.iov_len = size;

    struct iovec remote_iov;
    remote_iov.iov_base = reinterpret_cast<void*>(address);
    remote_iov.iov_len = size;

    ssize_t bytes_read = process_vm_readv(m_attachedPid, &local_iov, 1, &remote_iov, 1, 0);

    return bytes_read == static_cast<ssize_t>(size);
}

bool LinuxMemory::WriteMemory(uintptr_t address, const void* buffer, size_t size) {
    if (!m_isAttached || buffer == nullptr || size == 0) {
        return false;
    }

    struct iovec local_iov;
    local_iov.iov_base = const_cast<void*>(buffer);
    local_iov.iov_len = size;

    struct iovec remote_iov;
    remote_iov.iov_base = reinterpret_cast<void*>(address);
    remote_iov.iov_len = size;

    ssize_t bytes_written = process_vm_writev(m_attachedPid, &local_iov, 1, &remote_iov, 1, 0);

    return bytes_written == static_cast<ssize_t>(size);
}

void LinuxMemory::DetachProcess() {
    m_attachedPid = -1;
    m_isAttached = false;
}

bool LinuxMemory::IsProcessAttached() const {
    return m_isAttached;
}

pid_t LinuxMemory::GetAttachedPid() const {
    return m_isAttached ? m_attachedPid : -1;
}

bool LinuxMemory::IsProcessValid(pid_t pid) const {
    if (pid <= 0) {
        return false;
    }

    std::string proc_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream proc_file(proc_path);
    return proc_file.good();
}

} // namespace dqxclarity