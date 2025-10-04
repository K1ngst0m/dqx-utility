#pragma once

#include "../memory/IProcessMemory.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace dqxclarity {

struct HookState {
    uintptr_t hook_address;
    std::vector<uint8_t> original_bytes;
    bool is_hooked;
};

class HookManager {
public:
    HookManager();
    ~HookManager();

    bool SaveHookState(uintptr_t address, const std::vector<uint8_t>& original_bytes);
    bool LoadHookState(HookState& state);
    bool RestoreOriginalMemory(std::shared_ptr<IProcessMemory> memory);
    void ClearHookState();
    
    bool HasSavedState() const;

private:
    std::string m_state_file;
    
    bool WriteStateToFile(const HookState& state);
    bool ReadStateFromFile(HookState& state);
};

} // namespace dqxclarity