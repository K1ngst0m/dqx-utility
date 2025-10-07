#pragma once

#include "../memory/IProcessMemory.hpp"
#include "Codegen.hpp"
#include "../console/IConsoleSink.hpp"
#include "../api/dqxclarity.hpp"
#include "../memory/MemoryPatch.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <sstream>

namespace dqxclarity {

class DialogHook {
public:
    DialogHook(std::shared_ptr<IProcessMemory> memory);
    ~DialogHook();

    bool InstallHook(bool enable_patch = true);
    bool EnablePatch();
    bool RemoveHook();
    bool IsHookInstalled() const { return m_is_installed; }

    void SetLogger(const dqxclarity::Logger& log) { m_logger = log; }

    // Re-apply the JMP patch at the original hook site (used after integrity runs)
    bool ReapplyPatch();

    // Verify if the hook site currently contains our JMP -> detour bytes
    bool IsPatched() const;

    void SetVerbose(bool enabled) { m_verbose = enabled; }
    void SetConsoleOutput(bool enabled) { m_console_output = enabled; }
    void SetConsole(ConsolePtr console) { m_console = std::move(console); }

    void SetInstructionSafeSteal(bool enabled) { m_instr_safe = enabled; }
    void SetReadbackBytes(size_t n) { m_readback_n = n; }
    
    // Poll for new dialog data (call this periodically in a loop)
    bool PollDialogData();

    // Get the last captured dialog text
    std::string GetLastDialogText() const { return m_last_dialog_text; }
    std::string GetLastNpcName() const { return m_last_npc_name; }

    // Expose hook site and original bytes for integrity restoration
    uintptr_t GetHookAddress() const { return m_hook_address; }
    const std::vector<uint8_t>& GetOriginalBytes() const { return m_original_bytes; }

private:
    std::shared_ptr<IProcessMemory> m_memory;
    bool m_is_installed;
    
    // Hook addresses and data
    uintptr_t m_hook_address;
    uintptr_t m_detour_address;
    uintptr_t m_backup_address;
    std::vector<uint8_t> m_original_bytes;

    // Options
    bool m_verbose = false;
    bool m_console_output = false;
    ConsolePtr m_console;
    dqxclarity::Logger m_logger{};
    bool m_instr_safe = true;
    size_t m_readback_n = 16;
    
    // Dialog data
    mutable std::string m_last_dialog_text;
    mutable std::string m_last_npc_name;
    
    bool FindDialogTriggerAddress();
    bool AllocateDetourMemory();
    bool WriteDetourCode();
    bool PatchOriginalFunction();
    void RestoreOriginalFunction();
    
    // Note: Dialog extraction is now done via polling (PollDialogData), not direct calls
    
    std::vector<uint8_t> CreateDetourBytecode();
    uintptr_t CalculateRelativeAddress(uintptr_t from, uintptr_t to);

    size_t ComputeStolenLength();
};

} // namespace dqxclarity
