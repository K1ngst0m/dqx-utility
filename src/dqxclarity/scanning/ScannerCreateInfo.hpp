#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../pattern/Pattern.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../api/dqxclarity.hpp"

#include <chrono>
#include <functional>
#include <vector>

namespace dqxclarity
{

struct ScannerCreateInfo
{
    IProcessMemory* memory = nullptr;

    dqxclarity::Logger logger = {};
    bool verbose = false;

    Pattern pattern = {};
    std::chrono::milliseconds poll_interval{ 250 };

    std::vector<MemoryRegion> cached_regions = {};

    std::function<void(bool)> state_change_callback;
};

} // namespace dqxclarity

