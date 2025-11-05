#pragma once

#include "../memory/IProcessMemory.hpp"
#include "../pattern/Pattern.hpp"
#include "../api/dqxclarity.hpp"

#include <chrono>
#include <functional>

namespace dqxclarity
{

struct ScannerCreateInfo
{
    IProcessMemory* memory = nullptr;

    dqxclarity::Logger logger = {};
    bool verbose = false;

    Pattern pattern = {};
    std::chrono::milliseconds poll_interval{ 250 };

    std::function<void()> on_state_changed;
};

} // namespace dqxclarity

