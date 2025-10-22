#include "CornerTextHook.hpp"

#include "../signatures/Signatures.hpp"
#include "../pattern/PatternFinder.hpp"
#include "../util/Profile.hpp"

#include <cstring>
#include <sstream>

namespace dqxclarity
{

namespace
{

void LogCornerTextCapture(uintptr_t address, const std::string& text, const dqxclarity::Logger& logger)
{
    std::ostringstream oss;
    oss << "Corner text capture\n"
        << "  Address: 0x" << std::hex << address << std::dec << "\n"
        << "  Text: ";
    if (text.empty())
    {
        oss << "(empty)";
    }
    else
    {
        oss << text;
    }
    if (logger.info)
    {
        logger.info(oss.str());
    }
}

} // namespace

CornerTextHook::CornerTextHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(std::move(memory))
{
}

CornerTextHook::~CornerTextHook()
{
    if (m_is_installed)
    {
        RemoveHook();
    }
}

bool CornerTextHook::InstallHook(bool enable_patch)
{
    if (m_is_installed && enable_patch)
    {
        return true;
    }

    if (!FindCornerTriggerAddress())
    {
        if (m_logger.error)
            m_logger.error("Failed to find corner text trigger address");
        return false;
    }

    if (!AllocateDetourMemory())
    {
        if (m_logger.error)
            m_logger.error("Failed to allocate corner text detour memory");
        return false;
    }

    size_t stolen_bytes = ComputeStolenLength();
    if (stolen_bytes < 5)
    {
        stolen_bytes = kDefaultStolenBytes;
    }

    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to read corner text hook original bytes");
        return false;
    }

    if (!WriteDetourCode())
    {
        if (m_logger.error)
            m_logger.error("Failed to write corner text detour code");
        return false;
    }

    if (!enable_patch)
    {
        return true;
    }

    return EnablePatch();
}

bool CornerTextHook::EnablePatch()
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
            m_logger.error("Failed to write corner text hook patch bytes");
        return false;
    }

    if (m_verbose && m_logger.info)
    {
        std::ostringstream oss;
        oss << "Corner text hook patched at 0x" << std::hex << m_hook_address << std::dec;
        m_logger.info(oss.str());
    }

    m_is_installed = true;
    return true;
}

bool CornerTextHook::RemoveHook()
{
    if (!m_is_installed)
    {
        return true;
    }

    RestoreOriginalFunction();

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

    m_is_installed = false;
    return true;
}

bool CornerTextHook::ReapplyPatch()
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
            m_logger.error("Failed to reapply corner text hook patch");
        return false;
    }

    return true;
}

bool CornerTextHook::IsPatched() const
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

bool CornerTextHook::PollCornerText()
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

    uint32_t text_ptr_raw = 0;
    if (!m_memory->ReadMemory(m_backup_address, &text_ptr_raw, sizeof(text_ptr_raw)))
    {
        uint8_t zero = 0;
        m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));
        return false;
    }

    uint8_t zero = 0;
    m_memory->WriteMemory(m_backup_address + kFlagOffset, &zero, sizeof(zero));

    if (text_ptr_raw == 0)
    {
        return false;
    }

    const uintptr_t text_ptr = static_cast<uintptr_t>(text_ptr_raw);
    std::string text;
    if (!m_memory->ReadString(text_ptr, text, kMaxStringLength))
    {
        text.clear();
    }

    m_last_text = std::move(text);

    LogCornerTextCapture(text_ptr, m_last_text, m_logger);
    return true;
}

bool CornerTextHook::FindCornerTriggerAddress()
{
    PROFILE_SCOPE_FUNCTION();
    PatternFinder finder(m_memory);
    const auto& pattern = Signatures::GetCornerText();

    // Tier 1: Module-restricted scan
    {
        PROFILE_SCOPE_CUSTOM("CornerTextHook.FindInModule");
        if (auto addr = finder.FindInModule(pattern, "DQXGame.exe"))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Corner text trigger found via FindInModule (Tier 1)");
            return true;
        }
    }

    // Tier 2: Executable region scan
    {
        PROFILE_SCOPE_CUSTOM("CornerTextHook.FindInProcessExec");
        if (auto addr = finder.FindInProcessExec(pattern))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Corner text trigger found via FindInProcessExec (Tier 2)");
            return true;
        }
    }

    // Tier 3: Naive fallback scan (SLOW)
    {
        PROFILE_SCOPE_CUSTOM("CornerTextHook.FindWithFallback");
        if (m_logger.warn)
            m_logger.warn("Corner text trigger not found in Tier 1/2, falling back to naive scan (Tier 3)");

        if (auto addr = finder.FindWithFallback(pattern, "DQXGame.exe", 64u * 1024u * 1024u))
        {
            m_hook_address = *addr;
            if (m_verbose && m_logger.info)
                m_logger.info("Corner text trigger found via FindWithFallback (Tier 3 - naive scan)");
            return true;
        }
    }

    return false;
}

bool CornerTextHook::AllocateDetourMemory()
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

bool CornerTextHook::WriteDetourCode()
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

bool CornerTextHook::PatchOriginalFunction() { return EnablePatch(); }

void CornerTextHook::RestoreOriginalFunction()
{
    if (m_hook_address != 0 && !m_original_bytes.empty())
    {
        m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size());
    }
}

std::vector<uint8_t> CornerTextHook::CreateDetourBytecode()
{
    std::vector<uint8_t> code;
    EmitRegisterBackup(code);
    EmitNewDataFlag(code);
    EmitRegisterRestore(code);
    EmitStolenInstructions(code);
    EmitReturnJump(code);
    return code;
}

void CornerTextHook::EmitRegisterBackup(std::vector<uint8_t>& code)
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

void CornerTextHook::EmitRegisterRestore(std::vector<uint8_t>& code)
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

void CornerTextHook::EmitNewDataFlag(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.setByteAtMem(static_cast<uint32_t>(m_backup_address + kFlagOffset), 0x01);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void CornerTextHook::EmitStolenInstructions(std::vector<uint8_t>& code)
{
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
}

void CornerTextHook::EmitReturnJump(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    const uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    const uintptr_t jmp_from = m_detour_address + code.size();
    builder.jmpRel32(jmp_from, return_addr);
    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

size_t CornerTextHook::ComputeStolenLength()
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
