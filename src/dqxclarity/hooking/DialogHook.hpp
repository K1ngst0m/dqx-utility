#pragma once

#include "../memory/IProcessMemory.hpp"
#include "Codegen.hpp"
#include "../console/IConsoleSink.hpp"
#include "../api/dqxclarity.hpp"
#include "../memory/MemoryPatch.hpp"
#include "../pattern/MemoryRegion.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <sstream>
#include <functional>

namespace dqxclarity
{

class DialogHook
{
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

    using OriginalBytesCallback = std::function<void(uintptr_t, const std::vector<uint8_t>&)>;

    void SetOriginalBytesChangedCallback(OriginalBytesCallback cb) { m_original_bytes_cb = std::move(cb); }

    using HookSiteChangedCallback = std::function<void(uintptr_t, uintptr_t, const std::vector<uint8_t>&)>;

    void SetHookSiteChangedCallback(HookSiteChangedCallback cb) { m_site_changed_cb = std::move(cb); }

    void SetInstructionSafeSteal(bool enabled) { m_instr_safe = enabled; }

    void SetReadbackBytes(size_t n) { m_readback_n = n; }

    // Set pre-parsed memory regions to avoid repeated ParseMaps calls
    void SetCachedRegions(const std::vector<MemoryRegion>& regions) { m_cached_regions = regions; }

    // Poll for new dialog data (call this periodically in a loop)
    bool PollDialogData();

    // Get the last captured dialog text
    std::string GetLastDialogText() const { return m_last_dialog_text; }

    std::string GetLastNpcName() const { return m_last_npc_name; }

    // Expose hook site and original bytes for integrity restoration
    uintptr_t GetHookAddress() const { return m_hook_address; }

    const std::vector<uint8_t>& GetOriginalBytes() const { return m_original_bytes; }

    uintptr_t GetDetourAddress() const { return m_detour_address; }
    uintptr_t GetBackupAddress() const { return m_backup_address; }

private:
    static constexpr size_t kMaxStringLength = 4096;

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

    // Cached memory regions
    std::vector<MemoryRegion> m_cached_regions;

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

    // Helper methods for CreateDetourBytecode
    void EmitRegisterBackup(std::vector<uint8_t>& code);
    void EmitRegisterRestore(std::vector<uint8_t>& code);
    void EmitNewDataFlag(std::vector<uint8_t>& code);
    void EmitStolenInstructions(std::vector<uint8_t>& code);
    void EmitReturnJump(std::vector<uint8_t>& code);

    bool RefreshOriginalBytes();

    OriginalBytesCallback m_original_bytes_cb;
    HookSiteChangedCallback m_site_changed_cb;
};

} // namespace dqxclarity
