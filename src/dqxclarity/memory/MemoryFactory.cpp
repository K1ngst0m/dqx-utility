#include "MemoryFactory.hpp"
#include "LinuxMemory.hpp"

namespace dqxclarity {

std::unique_ptr<IProcessMemory> MemoryFactory::CreatePlatformMemory() {
#ifdef _WIN32
    // TODO: Return Windows implementation when available
    return nullptr;
#else
    return std::make_unique<LinuxMemory>();
#endif
}

} // namespace dqxclarity