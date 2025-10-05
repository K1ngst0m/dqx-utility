#include "MemoryPatch.hpp"
#include <algorithm>
#include <cstdio>

namespace dqxclarity {

bool MemoryPatch::WriteWithProtect(IProcessMemory& mem, uintptr_t address, const uint8_t* data, size_t size,
                                   MemoryProtectionFlags temp, MemoryProtectionFlags restore) {
    (void)mem.SetMemoryProtection(address, size, temp);
    bool ok = mem.WriteMemory(address, data, size);
    (void)mem.SetMemoryProtection(address, size, restore);
    if (ok) mem.FlushInstructionCache(address, size);
    return ok;
}

std::vector<uint8_t> MemoryPatch::ReadBack(IProcessMemory& mem, uintptr_t address, size_t size) {
    std::vector<uint8_t> out(size);
    if (!mem.ReadMemory(address, out.data(), out.size())) out.clear();
    return out;
}

std::string MemoryPatch::HexFirstN(const std::vector<uint8_t>& bytes, size_t n) {
    char buf[256];
    size_t cap = sizeof(buf);
    size_t pos = 0;
    size_t count = (std::min)(n, bytes.size());
    for (size_t i = 0; i < count && pos + 3 < cap; ++i) {
        pos += std::snprintf(buf + pos, cap - pos, "%02X ", bytes[i]);
    }
    return std::string(buf, buf + pos);
}

} // namespace dqxclarity