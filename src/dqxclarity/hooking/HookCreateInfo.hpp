#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../api/dqxclarity.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace dqxclarity
{

struct HookCreateInfo
{
    IProcessMemory* memory = nullptr;

    dqxclarity::Logger logger = {};
    bool verbose = false;
    bool instruction_safe_steal = true;
    size_t readback_bytes = 16;
    std::vector<MemoryRegion> cached_regions = {};

    // Integrity system callbacks
    std::function<void(uintptr_t address, const std::vector<uint8_t>& bytes)> on_original_bytes_changed;
    std::function<void(uintptr_t old_address, uintptr_t new_address, const std::vector<uint8_t>& bytes)> on_hook_site_changed;
};

} // namespace dqxclarity

