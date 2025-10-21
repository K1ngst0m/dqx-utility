#pragma once

#include "IProcessMemory.hpp"
#include <memory>

namespace dqxclarity
{

class MemoryFactory
{
public:
    static std::unique_ptr<IProcessMemory> CreatePlatformMemory();
};

} // namespace dqxclarity