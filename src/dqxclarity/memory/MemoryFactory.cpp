#include "MemoryFactory.hpp"
#ifdef _WIN32
#include "win/ProcessMemory.hpp"
#else
#include "linux/ProcessMemory.hpp"
#endif
#include <memory>

namespace dqxclarity {

std::unique_ptr<IProcessMemory> MemoryFactory::CreatePlatformMemory() {
#ifdef _WIN32
    return std::make_unique<ProcessMemory>();
#else
    return std::make_unique<ProcessMemory>();
#endif
}

} // namespace dqxclarity
