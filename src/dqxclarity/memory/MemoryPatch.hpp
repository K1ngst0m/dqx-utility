#pragma once

#include "IProcessMemory.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace dqxclarity {

class MemoryPatch {
public:
    static bool WriteWithProtect(IProcessMemory& mem, uintptr_t address, const uint8_t* data, size_t size,
                                 MemoryProtectionFlags temp = MemoryProtectionFlags::ReadWriteExecute,
                                 MemoryProtectionFlags restore = MemoryProtectionFlags::ReadExecute);

    static bool WriteWithProtect(IProcessMemory& mem, uintptr_t address, const std::vector<uint8_t>& bytes,
                                 MemoryProtectionFlags temp = MemoryProtectionFlags::ReadWriteExecute,
                                 MemoryProtectionFlags restore = MemoryProtectionFlags::ReadExecute) {
        return WriteWithProtect(mem, address, bytes.data(), bytes.size(), temp, restore);
    }

    static std::vector<uint8_t> ReadBack(IProcessMemory& mem, uintptr_t address, size_t size);

    static std::string HexFirstN(const std::vector<uint8_t>& bytes, size_t n = 16);
};

} // namespace dqxclarity