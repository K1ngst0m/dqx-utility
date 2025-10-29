#pragma once

#include "../memory/IProcessMemory.hpp"
#include "Codegen.hpp"
#include "../memory/MemoryPatch.hpp"
#include "../api/dqxclarity.hpp"
#include "../api/player_info.hpp"
#include "../pattern/MemoryRegion.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dqxclarity
{

class PlayerHook
{
public:
    explicit PlayerHook(std::shared_ptr<IProcessMemory> memory);
    ~PlayerHook();

    bool InstallHook(bool enable_patch = true);
    bool EnablePatch();
    bool RemoveHook();
    bool ReapplyPatch();
    bool IsPatched() const;

    void SetLogger(const dqxclarity::Logger& log) { m_logger = log; }

    void SetVerbose(bool enabled) { m_verbose = enabled; }

    void SetInstructionSafeSteal(bool enabled) { m_instr_safe = enabled; }

    void SetReadbackBytes(size_t n) { m_readback_n = n; }

    void SetCachedRegions(const std::vector<MemoryRegion>& regions) { m_cached_regions = regions; }

    bool PollPlayerData();

    const PlayerInfo& GetLastPlayer() const { return m_last_data; }

    uintptr_t GetHookAddress() const { return m_hook_address; }

    const std::vector<uint8_t>& GetOriginalBytes() const { return m_original_bytes; }

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxStringLength = 128;
    static constexpr size_t kDefaultStolenBytes = 6;
    static constexpr uint32_t kPlayerNameOffset = 24;
    static constexpr uint32_t kSiblingNameOffset = 100;
    static constexpr uint32_t kRelationshipOffset = 119;

    PlayerRelationship DecodeRelationship(uint8_t value) const;

    bool FindPlayerTriggerAddress();
    bool AllocateDetourMemory();
    bool WriteDetourCode();
    bool PatchOriginalFunction();
    void RestoreOriginalFunction();

    std::vector<uint8_t> CreateDetourBytecode();
    void EmitRegisterBackup(std::vector<uint8_t>& code);
    void EmitRegisterRestore(std::vector<uint8_t>& code);
    void EmitNewDataFlag(std::vector<uint8_t>& code);
    void EmitStolenInstructions(std::vector<uint8_t>& code);
    void EmitReturnJump(std::vector<uint8_t>& code);

    size_t ComputeStolenLength();

    std::shared_ptr<IProcessMemory> m_memory;
    bool m_is_installed = false;

    uintptr_t m_hook_address = 0;
    uintptr_t m_detour_address = 0;
    uintptr_t m_backup_address = 0;
    std::vector<uint8_t> m_original_bytes;

    bool m_verbose = false;
    bool m_instr_safe = true;
    size_t m_readback_n = 16;
    dqxclarity::Logger m_logger{};

    std::vector<MemoryRegion> m_cached_regions;
    PlayerInfo m_last_data;
    bool m_logged_once = false;
};

} // namespace dqxclarity
