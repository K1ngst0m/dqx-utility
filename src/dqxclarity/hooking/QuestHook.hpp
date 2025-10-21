#pragma once

#include "../memory/IProcessMemory.hpp"
#include "Codegen.hpp"
#include "../memory/MemoryPatch.hpp"
#include "../api/dqxclarity.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dqxclarity
{

class QuestHook
{
public:
    struct QuestData
    {
        std::string subquest_name;
        std::string quest_name;
        std::string description;
        std::string rewards;
        std::string repeat_rewards;
    };

    explicit QuestHook(std::shared_ptr<IProcessMemory> memory);
    ~QuestHook();

    bool InstallHook(bool enable_patch = true);
    bool EnablePatch();
    bool RemoveHook();
    bool ReapplyPatch();
    bool IsPatched() const;

    void SetLogger(const dqxclarity::Logger& log) { m_logger = log; }

    void SetVerbose(bool enabled) { m_verbose = enabled; }

    void SetInstructionSafeSteal(bool enabled) { m_instr_safe = enabled; }

    void SetReadbackBytes(size_t n) { m_readback_n = n; }

    bool PollQuestData();

    const QuestData& GetLastQuest() const { return m_last_data; }

    uintptr_t GetHookAddress() const { return m_hook_address; }

    const std::vector<uint8_t>& GetOriginalBytes() const { return m_original_bytes; }

private:
    static constexpr size_t kFlagOffset = 32;
    static constexpr size_t kMaxStringLength = 2048;
    static constexpr size_t kDefaultStolenBytes = 6;

    // Offsets discovered from reference implementation
    static constexpr uint32_t kSubquestNameOffset = 20;
    static constexpr uint32_t kQuestNameOffset = 76;
    static constexpr uint32_t kDescriptionOffset = 132;
    static constexpr uint32_t kRewardsOffset = 640;
    static constexpr uint32_t kRepeatRewardsOffset = 744;

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

    QuestData m_last_data;

    bool FindQuestTriggerAddress();
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
};

} // namespace dqxclarity
