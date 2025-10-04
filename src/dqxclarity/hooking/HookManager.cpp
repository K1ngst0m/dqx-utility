#include "HookManager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace dqxclarity {

HookManager::HookManager() 
    : m_state_file("hook_state.dat") {
}

HookManager::~HookManager() {
    // Optionally restore on destruction
}

bool HookManager::SaveHookState(uintptr_t address, const std::vector<uint8_t>& original_bytes) {
    HookState state;
    state.hook_address = address;
    state.original_bytes = original_bytes;
    state.is_hooked = true;
    
    if (WriteStateToFile(state)) {
        std::cout << "Hook state saved to " << m_state_file << "\n";
        return true;
    }
    
    std::cout << "Failed to save hook state\n";
    return false;
}

bool HookManager::LoadHookState(HookState& state) {
    return ReadStateFromFile(state);
}

bool HookManager::RestoreOriginalMemory(std::shared_ptr<IProcessMemory> memory) {
    HookState state;
    if (!ReadStateFromFile(state)) {
        std::cout << "No hook state to restore\n";
        return false;
    }
    
    if (!state.is_hooked || state.original_bytes.empty()) {
        std::cout << "Invalid hook state\n";
        return false;
    }
    
    // Restore original bytes
    if (memory->WriteMemory(state.hook_address, state.original_bytes.data(), state.original_bytes.size())) {
        std::cout << "Restored original memory at 0x" << std::hex << state.hook_address << std::dec << "\n";
        ClearHookState();
        return true;
    }
    
    std::cout << "Failed to restore memory at 0x" << std::hex << state.hook_address << std::dec << "\n";
    return false;
}

void HookManager::ClearHookState() {
    try {
        if (std::filesystem::exists(m_state_file)) {
            std::filesystem::remove(m_state_file);
            std::cout << "Cleared hook state file\n";
        }
    } catch (...) {
        std::cout << "Failed to clear hook state file\n";
    }
}

bool HookManager::HasSavedState() const {
    return std::filesystem::exists(m_state_file);
}

bool HookManager::WriteStateToFile(const HookState& state) {
    try {
        std::ofstream file(m_state_file, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Write hook address
        file.write(reinterpret_cast<const char*>(&state.hook_address), sizeof(state.hook_address));
        
        // Write is_hooked flag
        file.write(reinterpret_cast<const char*>(&state.is_hooked), sizeof(state.is_hooked));
        
        // Write original bytes size
        size_t bytes_size = state.original_bytes.size();
        file.write(reinterpret_cast<const char*>(&bytes_size), sizeof(bytes_size));
        
        // Write original bytes
        if (bytes_size > 0) {
            file.write(reinterpret_cast<const char*>(state.original_bytes.data()), bytes_size);
        }
        
        file.close();
        return true;
        
    } catch (...) {
        return false;
    }
}

bool HookManager::ReadStateFromFile(HookState& state) {
    try {
        std::ifstream file(m_state_file, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read hook address
        file.read(reinterpret_cast<char*>(&state.hook_address), sizeof(state.hook_address));
        
        // Read is_hooked flag
        file.read(reinterpret_cast<char*>(&state.is_hooked), sizeof(state.is_hooked));
        
        // Read original bytes size
        size_t bytes_size;
        file.read(reinterpret_cast<char*>(&bytes_size), sizeof(bytes_size));
        
        // Read original bytes
        if (bytes_size > 0) {
            state.original_bytes.resize(bytes_size);
            file.read(reinterpret_cast<char*>(state.original_bytes.data()), bytes_size);
        }
        
        file.close();
        return file.good() || file.eof();
        
    } catch (...) {
        return false;
    }
}

} // namespace dqxclarity