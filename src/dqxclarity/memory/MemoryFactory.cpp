#include "MemoryFactory.hpp"
#include "ProcessMemory.hpp"
#include <memory>

namespace dqxclarity
{

std::unique_ptr<IProcessMemory> MemoryFactory::CreatePlatformMemory()
{
    // Unified implementation using libmem works on all platforms
    return std::make_unique<ProcessMemory>();
}

} // namespace dqxclarity
