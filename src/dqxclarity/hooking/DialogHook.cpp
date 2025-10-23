#include "DialogHook.hpp"
#include "../signatures/Signatures.hpp"
#include "../pattern/PatternFinder.hpp"
#include "Codegen.hpp"
#include "../util/Profile.hpp"
#include <iostream>
#include <cstdio>
#include <sstream>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dqxclarity
{

DialogHook::DialogHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(memory)
    , m_is_installed(false)
    , m_hook_address(0)
    , m_detour_address(0)
    , m_backup_address(0)
{
}

DialogHook::~DialogHook()
{
    if (m_is_installed)
    {
        RemoveHook();
    }
}

bool DialogHook::InstallHook(bool enable_patch)
{
    if (m_is_installed && enable_patch)
    {
        std::cout << "Hook already installed\n";
        return true;
    }

    if (m_verbose)
        std::cout << "Installing dialog hook...\n";

    // Step 1: Find the dialog trigger address
    if (!FindDialogTriggerAddress())
    {
        std::cout << "Failed to find dialog trigger address\n";
        if (m_logger.error)
            m_logger.error("Failed to find dialog trigger address");
        return false;
    }

    if (m_verbose)
        std::cout << "Dialog trigger found at: 0x" << std::hex << m_hook_address << std::dec << "\n";

    // DIAGNOSTIC: Read and log bytes at hook location
    if (m_verbose)
    {
        std::vector<uint8_t> hook_bytes(20);
        if (m_memory->ReadMemory(m_hook_address, hook_bytes.data(), 20))
        {
            std::cout << "Bytes at hook location: ";
            for (size_t i = 0; i < 20; ++i)
                printf("%02X ", hook_bytes[i]);
            std::cout << "\n";
        }
    }

    // Step 2: Allocate memory for detour
    if (!AllocateDetourMemory())
    {
        std::cout << "Failed to allocate detour memory\n";
        if (m_logger.error)
            m_logger.error("Failed to allocate detour memory");
        return false;
    }

    if (m_verbose)
    {
        std::cout << "Detour address: 0x" << std::hex << m_detour_address << std::dec << "\n";
        std::cout << "Backup address: 0x" << std::hex << m_backup_address << std::dec << "\n";
    }

    if (m_logger.info)
    {
        m_logger.info("Dialog trigger at 0x" + std::to_string((unsigned long long)m_hook_address));
        m_logger.info("Detour at 0x" + std::to_string((unsigned long long)m_detour_address));
        m_logger.info("Backup at 0x" + std::to_string((unsigned long long)m_backup_address));
    }

    // Step 3: Read original bytes FIRST (before writing detour)
    size_t stolen_bytes = m_instr_safe ? ComputeStolenLength() : static_cast<size_t>(10);
    if (stolen_bytes < 5)
        stolen_bytes = 10; // safety
    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes))
    {
        std::cout << "Failed to read original bytes\n";
        if (m_logger.error)
            m_logger.error("Failed to read original bytes at dialog site");
        return false;
    }

    if (m_verbose)
    {
        std::cout << "Original bytes (stolen=" << stolen_bytes << "): ";
        for (size_t i = 0; i < stolen_bytes; ++i)
            printf("%02X ", m_original_bytes[i]);
        std::cout << "\n";
    }

    // Step 4: Write detour code (now that we have stolen bytes)
    if (!WriteDetourCode())
    {
        std::cout << "Failed to write detour code\n";
        if (m_logger.error)
            m_logger.error("Failed to write dialog detour code");
        return false;
    }

    if (!enable_patch)
    {
        // Defer patching until first integrity run
        return true;
    }

    return EnablePatch();
}

bool DialogHook::EnablePatch()
{
    if (!RefreshOriginalBytes())
    {
        std::cout << "Failed to refresh dialog hook bytes before patch\n";
        if (m_logger.error)
            m_logger.error("Failed to refresh dialog hook bytes before patch");
        return false;
    }

    if (!PatchOriginalFunction())
    {
        std::cout << "Failed to patch original function\n";
        if (m_logger.error)
            m_logger.error("Failed to patch dialog function");
        return false;
    }

    // DIAGNOSTIC: Verify the patch was applied
    std::vector<uint8_t> patched_bytes(static_cast<size_t>(m_readback_n));
    if (m_memory->ReadMemory(m_hook_address, patched_bytes.data(), patched_bytes.size()))
    {
        std::cout << "Bytes after patching: ";
        for (size_t i = 0; i < patched_bytes.size(); ++i)
        {
            printf("%02X ", patched_bytes[i]);
        }
        std::cout << "\n";
    }

    m_is_installed = true;
    if (m_verbose)
        std::cout << "Dialog hook installed successfully!\n";
    return true;
}

bool DialogHook::RemoveHook()
{
    if (!m_is_installed)
    {
        return true;
    }

    try
    {
        if (m_verbose)
            std::cout << "Removing dialog hook...\n";
        RestoreOriginalFunction();

        // Free allocated memory
        if (m_detour_address != 0)
        {
            if (!m_memory->FreeMemory(m_detour_address, 4096))
            {
                if (m_logger.error)
                    m_logger.error("Failed to free detour memory during cleanup");
            }
            m_detour_address = 0;
        }

        if (m_backup_address != 0)
        {
            if (!m_memory->FreeMemory(m_backup_address, 256))
            {
                if (m_logger.error)
                    m_logger.error("Failed to free backup memory during cleanup");
            }
            m_backup_address = 0;
        }

        m_is_installed = false;
        if (m_verbose)
            std::cout << "Dialog hook removed successfully!\n";
        return true;
    }
    catch (const std::exception& e)
    {
        if (m_logger.error)
            m_logger.error("Exception during DialogHook cleanup: " + std::string(e.what()));
        m_is_installed = false; // Mark as not installed even on failure
        return false;
    }
    catch (...)
    {
        if (m_logger.error)
            m_logger.error("Unknown exception during DialogHook cleanup");
        m_is_installed = false;
        return false;
    }
}

bool DialogHook::FindDialogTriggerAddress()
{
    PROFILE_SCOPE_FUNCTION();
    auto pattern = Signatures::GetDialogTrigger();

    if (m_verbose)
    {
        std::cout << "Searching for dialog trigger using PatternScanner...\n";
        std::cout << "Pattern: ";
        for (size_t i = 0; i < pattern.Size(); ++i)
        {
            if (pattern.mask[i])
                printf("%02X ", pattern.bytes[i]);
            else
                printf("?? ");
        }
        std::cout << "\n";
    }

    PatternFinder finder(m_memory);
    bool found = false;

    // Tier 1: Prefer module-restricted scan (use cached regions if available)
    {
        PROFILE_SCOPE_CUSTOM("DialogHook.FindInModule");
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
            found = true;
            if (m_verbose && m_logger.info)
                m_logger.info("Dialog trigger found via FindInModule (Tier 1)");
        }
    }

    // Tier 2: Fallback to executable region scan
    if (!found)
    {
        PROFILE_SCOPE_CUSTOM("DialogHook.FindInProcessExec");
        if (auto addr2 = finder.FindInProcessExec(pattern))
        {
            m_hook_address = *addr2;
            found = true;
            if (m_verbose && m_logger.info)
                m_logger.info("Dialog trigger found via FindInProcessExec (Tier 2)");
        }
    }

    // Tier 3: Fallback to naive scan (SLOW)
    if (!found)
    {
        PROFILE_SCOPE_CUSTOM("DialogHook.FindWithFallback");
        if (m_logger.warn)
            m_logger.warn("Dialog trigger not found in Tier 1/2, falling back to naive scan (Tier 3)");

        uintptr_t base = m_memory->GetModuleBaseAddress("DQXGame.exe");
        if (base == 0)
        {
            std::cout << "Failed to get DQXGame.exe base address\n";
            if (m_logger.error)
                m_logger.error("Failed to get DQXGame.exe base address");
            return false;
        }
        if (auto fb = finder.FindWithFallback(pattern, "DQXGame.exe", 80u * 1024u * 1024u))
        {
            m_hook_address = *fb;
            found = true;
            if (m_verbose && m_logger.info)
                m_logger.info("Dialog trigger found via FindWithFallback (Tier 3 - naive scan)");
        }
    }

    if (!found)
    {
        std::cout << "Dialog trigger pattern not found in executable regions or manual scan\n";
        if (m_logger.error)
            m_logger.error("Dialog trigger pattern not found");
        return false;
    }

    uintptr_t base = m_memory->GetModuleBaseAddress("DQXGame.exe");
    if (base != 0 && m_verbose)
    {
        std::cout << "Dialog trigger at: 0x" << std::hex << m_hook_address << " (offset 0x" << (m_hook_address - base)
                  << ")" << std::dec << "\n";
    }

    // Read first 16 bytes for verification
    std::vector<uint8_t> head(16);
    if (m_memory->ReadMemory(m_hook_address, head.data(), head.size()))
    {
        if (m_verbose)
        {
            std::cout << "Bytes at trigger: ";
            for (size_t i = 0; i < head.size(); ++i)
                printf("%02X ", head[i]);
            std::cout << "\n";
        }
        // Light sanity check: FF 73 08, C7 45 F4 00 00 00 00 presence
        bool ok = head.size() >= 10 && head[0] == 0xFF && head[1] == 0x73 && head[2] == 0x08 && head[3] == 0xC7 &&
                  head[4] == 0x45 && head[5] == 0xF4 && head[6] == 0x00 && head[7] == 0x00 && head[8] == 0x00 &&
                  head[9] == 0x00;
        if (!ok)
        {
            std::cout << "WARNING: Trigger bytes do not match expected prologue; continuing, but may be unstable\n";
            if (m_logger.warn)
                m_logger.warn("Dialog trigger prologue differs from expected; continuing");
        }
    }

    return true;
}

bool DialogHook::AllocateDetourMemory()
{
    // Allocate memory for detour code (4KB should be enough)
    m_detour_address = m_memory->AllocateMemory(4096, true);
    if (m_detour_address == 0)
    {
        return false;
    }

    // Allocate memory for register backup (256 bytes)
    m_backup_address = m_memory->AllocateMemory(256, false);
    if (m_backup_address == 0)
    {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
        return false;
    }

    return true;
}

bool DialogHook::WriteDetourCode()
{
    auto bytecode = CreateDetourBytecode();

    if (m_verbose)
    {
        std::cout << "Detour bytecode size: " << bytecode.size() << " bytes\n";
        std::cout << "First 50 bytes of detour: ";
    }
    size_t bytes_to_print = bytecode.size() < 50 ? bytecode.size() : 50;
    if (m_verbose)
    {
        for (size_t i = 0; i < bytes_to_print; ++i)
            printf("%02X ", bytecode[i]);
        std::cout << "\n";
    }

    if (!m_memory->WriteMemory(m_detour_address, bytecode.data(), bytecode.size()))
    {
        std::cout << "Failed to write detour bytecode\n";
        return false;
    }

    // Flush instruction cache for detour code
    m_memory->FlushInstructionCache(m_detour_address, bytecode.size());

    return true;
}

std::vector<uint8_t> DialogHook::CreateDetourBytecode()
{
    std::vector<uint8_t> code;

    EmitRegisterBackup(code);
    EmitNewDataFlag(code);
    EmitRegisterRestore(code);
    EmitStolenInstructions(code);
    EmitReturnJump(code);

    return code;
}

bool DialogHook::PatchOriginalFunction()
{
    // Note: m_original_bytes should already be populated by InstallHook()
    if (m_original_bytes.empty())
    {
        std::cout << "ERROR: Original bytes not captured before patching!\n";
        return false;
    }

    const size_t stolen_bytes = m_original_bytes.size();

    // Create JMP instruction to our detour
    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9); // JMP rel32

    uint32_t jump_offset = Rel32From(m_hook_address, m_detour_address);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<uint8_t*>(&jump_offset),
                       reinterpret_cast<uint8_t*>(&jump_offset) + 4);

    // Fill remaining bytes with NOPs
    while (patch_bytes.size() < stolen_bytes)
    {
        patch_bytes.push_back(0x90); // NOP
    }

    if (m_verbose)
    {
        std::cout << "Patching hook location with JMP to detour...\n";
        std::cout << "Jump offset: 0x" << std::hex << jump_offset << std::dec << "\n";
    }
    if (m_logger.info)
        m_logger.info("Patching dialog hook with JMP");

    // Ensure stolen bytes still match before patch
    {
        std::vector<uint8_t> cur(stolen_bytes);
        if (!m_memory->ReadMemory(m_hook_address, cur.data(), cur.size()))
        {
            std::cout << "Failed to read current bytes before patch\n";
            return false;
        }
        if (cur != m_original_bytes)
        {
            std::cout << "WARNING: Hook site bytes changed before patch; continuing cautiously\n";
            if (m_logger.warn)
                m_logger.warn("Hook site bytes changed before patch; continuing cautiously");
        }
    }

    // Write the patch with protection management
    if (!MemoryPatch::WriteWithProtect(*m_memory, m_hook_address, patch_bytes))
    {
        std::cout << "Failed to write patch bytes\n";
        if (m_logger.error)
            m_logger.error("Failed to write dialog JMP bytes");
        return false;
    }
    if (m_verbose)
        std::cout << "Instruction cache flushed\n";

    // DIAGNOSTIC: read-back
    {
        auto rb = MemoryPatch::ReadBack(*m_memory, m_hook_address, static_cast<size_t>(m_readback_n));
        if (!rb.empty())
        {
            if (m_verbose)
            {
                std::cout << "Hook bytes after patch: ";
                for (auto b : rb)
                    printf("%02X ", b);
                std::cout << "\n";
            }
            if (m_logger.info)
                m_logger.info(std::string("Hook bytes[0..") + std::to_string(m_readback_n) + "] " +
                              MemoryPatch::HexFirstN(rb, static_cast<size_t>(m_readback_n)));
        }
    }

    return true;
}

bool DialogHook::ReapplyPatch()
{
    if (!RefreshOriginalBytes())
    {
        if (m_logger.error)
            m_logger.error("Failed to refresh dialog hook bytes before reapply");
        return false;
    }

    if (!m_is_installed)
    {
        // Detour memory still present; just re-patch original site
        // We consider it okay to reapply regardless
    }

    const size_t stolen_bytes = m_original_bytes.size();
    if (stolen_bytes == 0)
        return false;

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9);
    uint32_t jump_offset = Rel32From(m_hook_address, m_detour_address);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<uint8_t*>(&jump_offset),
                       reinterpret_cast<uint8_t*>(&jump_offset) + 4);
    while (patch_bytes.size() < stolen_bytes)
        patch_bytes.push_back(0x90);

    if (m_verbose)
    {
        std::cout << "Reapplying hook JMP at 0x" << std::hex << m_hook_address << std::dec << " -> detour 0x"
                  << std::hex << m_detour_address << std::dec << " (rel=0x" << std::hex << jump_offset << std::dec
                  << ")\n";
    }

    if (m_logger.info)
        m_logger.info("Reapplying dialog hook JMP");

    if (!MemoryPatch::WriteWithProtect(*m_memory, m_hook_address, patch_bytes))
    {
        if (m_logger.error)
            m_logger.error("Failed to reapply dialog JMP");
        return false;
    }

    {
        auto rb = MemoryPatch::ReadBack(*m_memory, m_hook_address, static_cast<size_t>(m_readback_n));
        if (!rb.empty())
        {
            if (m_verbose)
            {
                std::cout << "Hook bytes after reapply: ";
                for (auto b : rb)
                    printf("%02X ", b);
                std::cout << "\n";
            }
            if (m_logger.info)
                m_logger.info(std::string("Reapplied bytes[0..") + std::to_string(m_readback_n) + "] " +
                              MemoryPatch::HexFirstN(rb, static_cast<size_t>(m_readback_n)));
        }
    }

    return true;
}

bool DialogHook::IsPatched() const
{
    if (m_hook_address == 0 || m_detour_address == 0 || m_original_bytes.empty())
        return false;
    const size_t n = m_original_bytes.size();
    std::vector<uint8_t> cur(n);
    if (!m_memory->ReadMemory(m_hook_address, cur.data(), cur.size()))
        return false;
    if (cur.size() < 5)
        return false;
    if (cur[0] != 0xE9)
        return false;
    uint32_t expected_rel = Rel32From(m_hook_address, m_detour_address);
    uint32_t got_rel = 0;
    std::memcpy(&got_rel, &cur[1], sizeof(uint32_t));
    if (got_rel != expected_rel)
        return false;
    // Ensure remaining bytes are NOPs (best-effort)
    for (size_t i = 5; i < cur.size(); ++i)
    {
        if (cur[i] != 0x90)
            return false;
    }
    return true;
}

void DialogHook::RestoreOriginalFunction()
{
    if (m_hook_address != 0 && !m_original_bytes.empty())
    {
        if (!m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size()))
        {
            if (m_logger.error)
                m_logger.error("Failed to restore original bytes at hook address during cleanup");
        }
    }
}

uintptr_t DialogHook::CalculateRelativeAddress(uintptr_t from, uintptr_t to) { return Rel32From(from, to); }

size_t DialogHook::ComputeStolenLength()
{
    // Try to read a small window and recognize the expected prologue
    std::vector<uint8_t> head(16);
    if (!m_memory->ReadMemory(m_hook_address, head.data(), head.size()))
    {
        return 10; // fallback
    }
    // Expected pattern: FF 73 08 C7 45 F4 00 00 00 00
    if (head.size() >= 10 && head[0] == 0xFF && head[1] == 0x73 && head[2] == 0x08 && head[3] == 0xC7 &&
        head[4] == 0x45 && head[5] == 0xF4 && head[6] == 0x00 && head[7] == 0x00 && head[8] == 0x00 && head[9] == 0x00)
    {
        return 10;
    }
    // Fallback: use 10 bytes like Python implementation to avoid splitting instructions
    if (m_logger.warn)
        m_logger.warn("Instruction-safe steal: unknown prologue; using 10 bytes fallback");
    return 10;
}

bool DialogHook::PollDialogData()
{
    if (!m_is_installed || m_backup_address == 0)
    {
        return false;
    }

    try
    {

        // Check if new dialog data flag is set (backup+32)
        uint8_t flag = 0;
        if (!m_memory->ReadMemory(m_backup_address + 32, &flag, 1))
        {
            return false;
        }

        if (flag == 0)
        {
            return false; // No new data
        }

        // Read the captured register values
        uintptr_t text_ptr = 0;
        uintptr_t npc_ptr = 0;

        if (!m_memory->ReadMemory(m_backup_address + 16, &text_ptr, 4))
        {
            return false;
        }

        if (!m_memory->ReadMemory(m_backup_address + 28, &npc_ptr, 4))
        {
            return false;
        }

        // Clear the flag
        uint8_t zero = 0;
        m_memory->WriteMemory(m_backup_address + 32, &zero, 1);

        // Extract text pointer and npc pointer following Python behavior:
        // text_ptr (ESI) is already the address of the text string
        uintptr_t text_address = text_ptr;

        // npc_ptr (ESP): read a 32-bit value at (ESP + 0x14)
        uintptr_t npc_address = m_memory->ReadInt32(npc_ptr + 0x14);

        // Read dialog text
        std::string dialog_text;
        if (text_address != 0)
        {
            m_memory->ReadString(text_address, dialog_text, kMaxStringLength);
        }

        // Read NPC name
        std::string npc_name = "No_NPC";
        if (npc_address != 0)
        {
            if (!m_memory->ReadString(npc_address, npc_name, kMaxStringLength) || npc_name.empty())
            {
                npc_name = "No_NPC";
            }
        }
        m_last_dialog_text = dialog_text;
        m_last_npc_name = npc_name;

        if (m_console_output && m_console && !dialog_text.empty())
        {
            m_console->PrintDialog(npc_name, dialog_text);
        }

        return true;
    }
    catch (...)
    {
        // Silently ignore errors to avoid crashes
        return false;
    }
}

void DialogHook::EmitRegisterBackup(std::vector<uint8_t>& code)
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

bool DialogHook::RefreshOriginalBytes()
{
    if (m_memory == nullptr)
    {
        return false;
    }

    if (IsPatched())
    {
        return true;
    }

    const auto& pattern = Signatures::GetDialogTrigger();

    auto matches_at = [&](uintptr_t addr) -> bool
    {
        if (addr == 0)
            return false;
        std::vector<uint8_t> buf(pattern.Size());
        if (!m_memory->ReadMemory(addr, buf.data(), buf.size()))
            return false;
        for (size_t i = 0; i < pattern.Size(); ++i)
        {
            if (!pattern.mask[i])
                continue;
            if (buf[i] != pattern.bytes[i])
                return false;
        }
        return true;
    };

    uintptr_t located_addr = 0;
    if (matches_at(m_hook_address))
    {
        located_addr = m_hook_address;
    }
    else
    {
        PatternFinder finder(m_memory);
        if (auto addr = finder.FindWithFallback(pattern, "DQXGame.exe"))
        {
            located_addr = *addr;
        }
    }

    if (located_addr == 0)
    {
        if (m_logger.error)
            m_logger.error("Dialog hook signature missing when refreshing");
        return false;
    }

    const bool address_changed = (located_addr != m_hook_address);
    uintptr_t previous_addr = m_hook_address;
    m_hook_address = located_addr;

    size_t new_len = m_instr_safe ? ComputeStolenLength() : static_cast<size_t>(10);
    if (new_len < 5)
        new_len = 10;

    std::vector<uint8_t> latest(new_len);
    if (!m_memory->ReadMemory(m_hook_address, latest.data(), latest.size()))
    {
        return false;
    }

    bool bytes_changed = m_original_bytes.size() != latest.size();
    if (!bytes_changed && !m_original_bytes.empty())
    {
        bytes_changed = !std::equal(latest.begin(), latest.end(), m_original_bytes.begin());
    }

    if (!address_changed && !bytes_changed)
    {
        return true;
    }

    if (bytes_changed && !m_original_bytes.empty())
    {
        size_t mismatch = 0;
        size_t limit = m_original_bytes.size();
        if (latest.size() < limit)
            limit = latest.size();
        while (mismatch < limit && m_original_bytes[mismatch] == latest[mismatch])
        {
            ++mismatch;
        }
        std::ostringstream oss;
        oss << "Dialog hook prologue changed; mismatch index=" << mismatch;
        if (m_logger.warn)
            m_logger.warn(oss.str());
        if (m_verbose)
        {
            std::cout << oss.str() << "\n";
            std::cout << "Old: ";
            for (auto b : m_original_bytes)
                printf("%02X ", b);
            std::cout << "\nNew: ";
            for (auto b : latest)
                printf("%02X ", b);
            std::cout << "\n";
        }
    }

    m_original_bytes = std::move(latest);

    if (!WriteDetourCode())
    {
        return false;
    }

    if (m_original_bytes_cb)
    {
        m_original_bytes_cb(m_hook_address, m_original_bytes);
    }
    if (address_changed && m_site_changed_cb)
    {
        m_site_changed_cb(previous_addr, m_hook_address, m_original_bytes);
    }

    return true;
}

void DialogHook::EmitRegisterRestore(std::vector<uint8_t>& code)
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

void DialogHook::EmitNewDataFlag(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    builder.setByteAtMem(static_cast<uint32_t>(m_backup_address + 32), 0x01);

    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

void DialogHook::EmitStolenInstructions(std::vector<uint8_t>& code)
{
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
}

void DialogHook::EmitReturnJump(std::vector<uint8_t>& code)
{
    X86CodeBuilder builder;
    uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    uintptr_t jmp_from = m_detour_address + code.size();
    builder.jmpRel32(jmp_from, return_addr);

    const auto& generated = builder.code();
    code.insert(code.end(), generated.begin(), generated.end());
}

} // namespace dqxclarity
