#include "PlayerHook.hpp"

#include "../signatures/Signatures.hpp"
#include "../pattern/PatternFinder.hpp"
#include "../util/Profile.hpp"

#include <cstring>
#include <sstream>
#include <optional>

namespace dqxclarity
{

inline const char* player_relationship_to_string(PlayerRelationship rel)
{
    switch (rel)
    {
    case PlayerRelationship::OlderBrother:
        return "older_brother";
    case PlayerRelationship::YoungerBrother:
        return "younger_brother";
    case PlayerRelationship::OlderSister:
        return "older_sister";
    case PlayerRelationship::YoungerSister:
        return "younger_sister";
    default:
        return "unknown";
    }
}

PlayerHook::PlayerHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(std::move(memory))
{
}

PlayerHook::~PlayerHook()
{
    if (m_is_installed)
    {
        RemoveHook();
    }
}

bool PlayerHook::InstallHook(bool enable_patch)
{
    if (m_is_installed && enable_patch)
    {
        return true;
    }

    if (!FindPlayerTriggerAddress())
    {
        if (m_logger.error)
            m_logger.error("Failed to find player name trigger address");
        return false;
    }

    if (!AllocateDetourMemory())
    {
        if (m_logger.error)
            m_logger.error("Failed to allocate player hook detour memory");
        return false;
    }

    const size_t stolen_bytes = ComputeStolenLength();
    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to read player hook original bytes");
        return false;
    }

    if (!WriteDetourCode())
    {
        if (m_logger.error)
            m_logger.error("Failed to write player hook detour code");
        return false;
    }

    if (!enable_patch)
    {
        return true;
    }

    return EnablePatch();
}

bool PlayerHook::EnablePatch()
{
    if (m_original_bytes.empty())
    {
        return false;
    }

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9);
    const uint32_t jump_offset = Rel32From(m_hook_address, m_detour_address);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<const uint8_t*>(&jump_offset),
                       reinterpret_cast<const uint8_t*>(&jump_offset) + sizeof(uint32_t));

    while (patch_bytes.size() < m_original_bytes.size())
    {
        patch_bytes.push_back(0x90);
    }

    if (!MemoryPatch::WriteWithProtect(*m_memory, m_hook_address, patch_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to write player hook patch bytes");
        return false;
    }

    if (m_verbose && m_logger.info)
    {
        std::ostringstream oss;
        oss << "Player hook patched at 0x" << std::hex << m_hook_address << std::dec;
        m_logger.info(oss.str());
    }

    m_is_installed = true;
    return true;
}

bool PlayerHook::RemoveHook()
{
    if (!m_is_installed)
    {
        return true;
    }

    RestoreOriginalFunction();
    m_is_installed = false;

    if (m_detour_address != 0)
    {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
    }
    if (m_backup_address != 0)
    {
        m_memory->FreeMemory(m_backup_address, 256);
        m_backup_address = 0;
    }

    m_original_bytes.clear();
    return true;
}

bool PlayerHook::ReapplyPatch()
{
    if (!m_is_installed)
    {
        return InstallHook(true);
    }
    return EnablePatch();
}

bool PlayerHook::IsPatched() const
{
    if (m_hook_address == 0 || m_detour_address == 0 || m_original_bytes.empty())
    {
        return false;
    }

    std::vector<uint8_t> cur(m_original_bytes.size());
    if (!m_memory->ReadMemory(m_hook_address, cur.data(), cur.size()))
    {
        return false;
    }
    if (cur.size() < 5 || cur[0] != 0xE9)
    {
        return false;
    }

    uint32_t rel = 0;
    std::memcpy(&rel, &cur[1], sizeof(uint32_t));
    return rel == Rel32From(m_hook_address, m_detour_address);
}

bool PlayerHook::PollPlayerData()
{
    if (!m_is_installed || m_backup_address == 0)
    {
        return false;
    }

    uint8_t flag = 0;
    if (!m_memory->ReadMemory(m_backup_address + kFlagOffset, &flag, sizeof(flag)))
    {
        return false;
    }

    if (flag == 0)
    {
        return false;
    }

    uint32_t ptr_raw = 0;
    if (!m_memory->ReadMemory(m_backup_address, &ptr_raw, sizeof(ptr_raw)))
    {
        uint8_t zero = 0;
        m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    if (ptr_raw == 0)
    {
        return false;
    }

    const uintptr_t struct_ptr = static_cast<uintptr_t>(ptr_raw);

    PlayerInfo data;
    auto read_field = [&](uint32_t offset, std::string& out)
    {
        std::string value;
        if (m_memory->ReadString(struct_ptr + offset, value, kMaxStringLength))
        {
            out = std::move(value);
        }
        else
        {
            out.clear();
        }
    };

    read_field(kPlayerNameOffset, data.player_name);
    read_field(kSiblingNameOffset, data.sibling_name);

    uint8_t rel_byte = 0;
    if (m_memory->ReadMemory(struct_ptr + kRelationshipOffset, &rel_byte, sizeof(rel_byte)))
    {
        data.relationship = DecodeRelationship(rel_byte);
    }
    else
    {
        data.relationship = PlayerRelationship::Unknown;
    }

    const bool changed = (data.player_name != m_last_data.player_name) ||
                         (data.sibling_name != m_last_data.sibling_name) ||
                         (data.relationship != m_last_data.relationship);

    const bool should_signal = changed || !m_logged_once;
    m_last_data = std::move(data);

    if (should_signal && m_logger.info)
    {
        std::ostringstream oss;
        oss << "Captured player info: player=\"" << m_last_data.player_name << "\", sibling=\""
            << m_last_data.sibling_name << "\", relationship=" << player_relationship_to_string(m_last_data.relationship);
        m_logger.info(oss.str());
    }

    if (should_signal)
    {
        m_logged_once = true;
    }

    return should_signal;
}

PlayerRelationship PlayerHook::DecodeRelationship(uint8_t value) const
{
    switch (value)
    {
    case 0x01:
        return PlayerRelationship::OlderBrother;
    case 0x02:
        return PlayerRelationship::YoungerBrother;
    case 0x03:
        return PlayerRelationship::OlderSister;
    case 0x04:
        return PlayerRelationship::YoungerSister;
    default:
        return PlayerRelationship::Unknown;
    }
}

bool PlayerHook::FindPlayerTriggerAddress()
{
    PROFILE_SCOPE_FUNCTION();
    PatternFinder finder(m_memory);
    const auto& pattern = Signatures::GetPlayerNameTrigger();

    // Tier 1: Module-restricted scan (use cached regions if available)
    {
        PROFILE_SCOPE_CUSTOM("PlayerHook.FindInModule");
        std::optional<uintptr_t> addr;

        if (!m_cached_regions.empty())
        {
            addr = finder.FindInModuleWithRegions(pattern, "DQXGame.exe", m_cached_regions);
        }
        else
        {
            addr = finder.FindInModule(pattern, "DQXGame.exe");
        }

        if (addr)
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Player trigger found via FindInModule (Tier 1)");
            return true;
        }
    }

    // Tier 2: Executable region scan
    {
        PROFILE_SCOPE_CUSTOM("PlayerHook.FindInProcessExec");
        std::optional<uintptr_t> addr = finder.FindInProcessExec(pattern);
        if (addr)
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Player trigger found via FindInProcessExec (Tier 2)");
            return true;
        }
    }

    return false;
}

bool PlayerHook::AllocateDetourMemory()
{
    m_detour_address = m_memory->AllocateMemory(4096, true);
    if (m_detour_address == 0)
    {
        return false;
    }

    m_backup_address = m_memory->AllocateMemory(256, false);
    if (m_backup_address == 0)
    {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    return true;
}

bool PlayerHook::WriteDetourCode()
{
    auto bytecode = CreateDetourBytecode();
    if (bytecode.empty())
    {
        return false;
    }

    if (!m_memory->WriteMemory(m_detour_address, bytecode.data(), bytecode.size()))
    {
        return false;
    }

    m_memory->FlushInstructionCache(m_detour_address, bytecode.size());
    return true;
}

bool PlayerHook::PatchOriginalFunction() { return EnablePatch(); }

void PlayerHook::RestoreOriginalFunction()
{
    if (m_hook_address != 0 && !m_original_bytes.empty())
    {
        m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size());
    }
}

std::vector<uint8_t> PlayerHook::CreateDetourBytecode()
{
    std::vector<uint8_t> code;
    EmitRegisterBackup(code);
    EmitNewDataFlag(code);
    EmitRegisterRestore(code);
    EmitStolenInstructions(code);
    EmitReturnJump(code);
    return code;
}

void PlayerHook::EmitRegisterBackup(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.movToMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(m_backup_address));
    builder.movToMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(m_backup_address + 4));
    builder.movToMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(m_backup_address + 8));
    builder.movToMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(m_backup_address + 12));
    builder.movToMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(m_backup_address + 16));
    builder.movToMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(m_backup_address + 20));
    builder.movToMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(m_backup_address + 24));
    builder.movToMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(m_backup_address + 28));
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void PlayerHook::EmitRegisterRestore(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.movFromMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(m_backup_address));
    builder.movFromMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(m_backup_address + 4));
    builder.movFromMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(m_backup_address + 8));
    builder.movFromMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(m_backup_address + 12));
    builder.movFromMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(m_backup_address + 16));
    builder.movFromMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(m_backup_address + 20));
    builder.movFromMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(m_backup_address + 24));
    builder.movFromMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(m_backup_address + 28));
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void PlayerHook::EmitNewDataFlag(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.setByteAtMem(static_cast<uint32_t>(m_backup_address + kFlagOffset), 0x01);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void PlayerHook::EmitStolenInstructions(std::vector<uint8_t>& code)
{
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
}

void PlayerHook::EmitReturnJump(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    const uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    const uintptr_t jmp_from = m_detour_address + code.size();
    builder.jmpRel32(jmp_from, return_addr);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

size_t PlayerHook::ComputeStolenLength()
{
    if (!m_instr_safe)
    {
        return kDefaultStolenBytes;
    }

    std::vector<uint8_t> head(kDefaultStolenBytes);
    if (!m_memory->ReadMemory(m_hook_address, head.data(), head.size()))
    {
        return kDefaultStolenBytes;
    }

    return kDefaultStolenBytes;
}

} // namespace dqxclarity
