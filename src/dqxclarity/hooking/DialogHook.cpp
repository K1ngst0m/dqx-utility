#include "DialogHook.hpp"
#include "../signatures/Signatures.hpp"
#include "../pattern/PatternScanner.hpp"
#include "Codegen.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dqxclarity {

DialogHook::DialogHook(std::shared_ptr<IProcessMemory> memory)
    : m_memory(memory)
    , m_is_installed(false)
    , m_hook_address(0)
    , m_detour_address(0)
    , m_backup_address(0) {
}

DialogHook::~DialogHook() {
    if (m_is_installed) {
        RemoveHook();
    }
}

bool DialogHook::InstallHook() {
    if (m_is_installed) {
        std::cout << "Hook already installed\n";
        return true;
    }

    if (m_verbose) std::cout << "Installing dialog hook...\n";

    // Step 1: Find the dialog trigger address
    if (!FindDialogTriggerAddress()) {
        std::cout << "Failed to find dialog trigger address\n";
        return false;
    }

    if (m_verbose) std::cout << "Dialog trigger found at: 0x" << std::hex << m_hook_address << std::dec << "\n";
    
    // DIAGNOSTIC: Read and log bytes at hook location
    if (m_verbose) {
        std::vector<uint8_t> hook_bytes(20);
        if (m_memory->ReadMemory(m_hook_address, hook_bytes.data(), 20)) {
            std::cout << "Bytes at hook location: ";
            for (size_t i = 0; i < 20; ++i) printf("%02X ", hook_bytes[i]);
            std::cout << "\n";
        }
    }

    // Step 2: Allocate memory for detour
    if (!AllocateDetourMemory()) {
        std::cout << "Failed to allocate detour memory\n";
        return false;
    }
    
    if (m_verbose) {
        std::cout << "Detour address: 0x" << std::hex << m_detour_address << std::dec << "\n";
        std::cout << "Backup address: 0x" << std::hex << m_backup_address << std::dec << "\n";
    }

    // Step 3: Read original bytes FIRST (before writing detour)
    // Use 10 bytes (minimum covering whole instructions: 3 + 7)
    //   FF 73 08                      (3 bytes) push [ebx+8]
    //   C7 45 F4 00 00 00 00          (7 bytes) mov [ebp-0Ch], 0
    const size_t stolen_bytes = 10;
    m_original_bytes.resize(stolen_bytes);
    if (!m_memory->ReadMemory(m_hook_address, m_original_bytes.data(), stolen_bytes)) {
        std::cout << "Failed to read original bytes\n";
        return false;
    }
    
    if (m_verbose) {
        std::cout << "Original bytes: ";
        for (size_t i = 0; i < stolen_bytes; ++i) printf("%02X ", m_original_bytes[i]);
        std::cout << "\n";
    }

    // Step 4: Write detour code (now that we have stolen bytes)
    if (!WriteDetourCode()) {
        std::cout << "Failed to write detour code\n";
        return false;
    }

    // Step 5: Patch original function
    if (!PatchOriginalFunction()) {
        std::cout << "Failed to patch original function\n";
        return false;
    }
    
    // DIAGNOSTIC: Verify the patch was applied
    std::vector<uint8_t> patched_bytes(20);
    if (m_memory->ReadMemory(m_hook_address, patched_bytes.data(), 20)) {
        std::cout << "Bytes after patching: ";
        for (size_t i = 0; i < 20; ++i) {
            printf("%02X ", patched_bytes[i]);
        }
        std::cout << "\n";
    }

    m_is_installed = true;
    if (m_verbose) std::cout << "Dialog hook installed successfully!\n";
    return true;
}

bool DialogHook::RemoveHook() {
    if (!m_is_installed) {
        return true;
    }

    if (m_verbose) std::cout << "Removing dialog hook...\n";
    RestoreOriginalFunction();
    
    // Free allocated memory
    if (m_detour_address != 0) {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
    }
    
    if (m_backup_address != 0) {
        m_memory->FreeMemory(m_backup_address, 256);
        m_backup_address = 0;
    }

    m_is_installed = false;
    if (m_verbose) std::cout << "Dialog hook removed successfully!\n";
    return true;
}

bool DialogHook::FindDialogTriggerAddress() {
    auto pattern = Signatures::GetDialogTrigger();
    uintptr_t base = m_memory->GetModuleBaseAddress("DQXGame.exe");
    
    if (base == 0) {
        std::cout << "Failed to get DQXGame.exe base address\n";
        return false;
    }
    
    if (m_verbose) {
        std::cout << "DQXGame.exe base: 0x" << std::hex << base << std::dec << "\n";
        std::cout << "Pattern: ";
    }
    if (m_verbose) {
        for (size_t i = 0; i < pattern.Size(); ++i) {
            if (pattern.mask[i]) printf("%02X ", pattern.bytes[i]); else printf("?? ");
        }
        std::cout << "\n";
    }
    
    const size_t scan_size = 50 * 1024 * 1024;
    const size_t chunk_size = 64 * 1024;
    std::vector<uint8_t> buffer(chunk_size);
    
    for (uintptr_t addr = base; addr < base + scan_size; addr += chunk_size) {
        if (!m_memory->ReadMemory(addr, buffer.data(), chunk_size)) {
            continue;
        }
        
        for (size_t i = 0; i <= chunk_size - pattern.Size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < pattern.Size(); ++j) {
                if (!pattern.mask[j]) continue;
                if (buffer[i + j] != pattern.bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                m_hook_address = addr + i;
                std::cout << "Found at offset: 0x" << std::hex << (m_hook_address - base) << std::dec << "\n";
                return true;
            }
        }
    }
    
    std::cout << "Pattern not found after scanning " << (scan_size / 1024 / 1024) << " MB\n";
    return false;
}

bool DialogHook::AllocateDetourMemory() {
    // Allocate memory for detour code (4KB should be enough)
    m_detour_address = m_memory->AllocateMemory(4096, true);
    if (m_detour_address == 0) {
        return false;
    }

    // Allocate memory for register backup (256 bytes)
    m_backup_address = m_memory->AllocateMemory(256, false);
    if (m_backup_address == 0) {
        m_memory->FreeMemory(m_detour_address, 4096);
        m_detour_address = 0;
        return false;
    }

    return true;
}

bool DialogHook::WriteDetourCode() {
    auto bytecode = CreateDetourBytecode();
    
    if (m_verbose) {
        std::cout << "Detour bytecode size: " << bytecode.size() << " bytes\n";
        std::cout << "First 50 bytes of detour: ";
    }
    size_t bytes_to_print = bytecode.size() < 50 ? bytecode.size() : 50;
    if (m_verbose) {
        for (size_t i = 0; i < bytes_to_print; ++i) printf("%02X ", bytecode[i]);
        std::cout << "\n";
    }
    
    if (!m_memory->WriteMemory(m_detour_address, bytecode.data(), bytecode.size())) {
        std::cout << "Failed to write detour bytecode\n";
        return false;
    }
    
    // Flush instruction cache for detour code
    m_memory->FlushInstructionCache(m_detour_address, bytecode.size());
    
    return true;
}

std::vector<uint8_t> DialogHook::CreateDetourBytecode() {
    std::vector<uint8_t> code;

    // Save registers
    code.insert(code.end(), {0xA3});
    uint32_t backup_eax = static_cast<uint32_t>(m_backup_address);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_eax), 
                reinterpret_cast<uint8_t*>(&backup_eax) + 4);
    code.push_back(0x90); // nop
    
    // mov [backup+4], ebx
    code.insert(code.end(), {0x89, 0x1D});
    uint32_t backup_ebx = static_cast<uint32_t>(m_backup_address + 4);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ebx), 
                reinterpret_cast<uint8_t*>(&backup_ebx) + 4);
    
    // mov [backup+8], ecx
    code.insert(code.end(), {0x89, 0x0D});
    uint32_t backup_ecx = static_cast<uint32_t>(m_backup_address + 8);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ecx), 
                reinterpret_cast<uint8_t*>(&backup_ecx) + 4);
    
    // mov [backup+12], edx
    code.insert(code.end(), {0x89, 0x15});
    uint32_t backup_edx = static_cast<uint32_t>(m_backup_address + 12);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_edx), 
                reinterpret_cast<uint8_t*>(&backup_edx) + 4);
    
    // mov [backup+16], esi  (this is the dialog text pointer!)
    code.insert(code.end(), {0x89, 0x35});
    uint32_t backup_esi = static_cast<uint32_t>(m_backup_address + 16);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_esi), 
                reinterpret_cast<uint8_t*>(&backup_esi) + 4);
    
    // mov [backup+20], edi
    code.insert(code.end(), {0x89, 0x3D});
    uint32_t backup_edi = static_cast<uint32_t>(m_backup_address + 20);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_edi), 
                reinterpret_cast<uint8_t*>(&backup_edi) + 4);
    
    // mov [backup+24], ebp
    code.insert(code.end(), {0x89, 0x2D});
    uint32_t backup_ebp = static_cast<uint32_t>(m_backup_address + 24);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ebp), 
                reinterpret_cast<uint8_t*>(&backup_ebp) + 4);
    
    // mov [backup+28], esp  (this is the NPC pointer!)
    code.insert(code.end(), {0x89, 0x25});
    uint32_t backup_esp = static_cast<uint32_t>(m_backup_address + 28);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_esp), 
                reinterpret_cast<uint8_t*>(&backup_esp) + 4);
    
    // New data flag
    code.insert(code.end(), {0xC6, 0x05});
    uint32_t flag_addr = static_cast<uint32_t>(m_backup_address + 32);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&flag_addr), 
                reinterpret_cast<uint8_t*>(&flag_addr) + 4);
    code.push_back(0x01);
    
    // Restore registers
    code.insert(code.end(), {0xA1});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_eax), 
                reinterpret_cast<uint8_t*>(&backup_eax) + 4);
    code.push_back(0x90); // nop
    
    // mov ebx, [backup+4]
    code.insert(code.end(), {0x8B, 0x1D});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ebx), 
                reinterpret_cast<uint8_t*>(&backup_ebx) + 4);
    
    // mov ecx, [backup+8]
    code.insert(code.end(), {0x8B, 0x0D});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ecx), 
                reinterpret_cast<uint8_t*>(&backup_ecx) + 4);
    
    // mov edx, [backup+12]
    code.insert(code.end(), {0x8B, 0x15});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_edx), 
                reinterpret_cast<uint8_t*>(&backup_edx) + 4);
    
    // mov esi, [backup+16]
    code.insert(code.end(), {0x8B, 0x35});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_esi), 
                reinterpret_cast<uint8_t*>(&backup_esi) + 4);
    
    // mov edi, [backup+20]
    code.insert(code.end(), {0x8B, 0x3D});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_edi), 
                reinterpret_cast<uint8_t*>(&backup_edi) + 4);
    
    // mov ebp, [backup+24]
    code.insert(code.end(), {0x8B, 0x2D});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_ebp), 
                reinterpret_cast<uint8_t*>(&backup_ebp) + 4);
    
    // mov esp, [backup+28]
    code.insert(code.end(), {0x8B, 0x25});
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&backup_esp), 
                reinterpret_cast<uint8_t*>(&backup_esp) + 4);
    
    // Execute original stolen bytes
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
    
    // Jump back to original function after the stolen bytes
    code.push_back(0xE9);
    uintptr_t return_addr = m_hook_address + m_original_bytes.size();
    uintptr_t jmp_from = m_detour_address + (code.size() - 1);
    uint32_t jump_offset = Rel32From(jmp_from, return_addr);
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&jump_offset), 
                reinterpret_cast<uint8_t*>(&jump_offset) + 4);
    
    return code;
}

bool DialogHook::PatchOriginalFunction() {
    // Note: m_original_bytes should already be populated by InstallHook()
    if (m_original_bytes.empty()) {
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
    while (patch_bytes.size() < stolen_bytes) {
        patch_bytes.push_back(0x90); // NOP
    }
    
    if (m_verbose) {
        std::cout << "Patching hook location with JMP to detour...\n";
        std::cout << "Jump offset: 0x" << std::hex << jump_offset << std::dec << "\n";
    }
    
    // Write the patch
    if (!m_memory->WriteMemory(m_hook_address, patch_bytes.data(), patch_bytes.size())) {
        std::cout << "Failed to write patch bytes\n";
        return false;
    }
    
    // Flush instruction cache to ensure CPU sees the new code
    m_memory->FlushInstructionCache(m_hook_address, stolen_bytes);
    if (m_verbose) std::cout << "Instruction cache flushed\n";
    
    return true;
}

void DialogHook::RestoreOriginalFunction() {
    if (m_hook_address != 0 && !m_original_bytes.empty()) {
        m_memory->WriteMemory(m_hook_address, m_original_bytes.data(), m_original_bytes.size());
    }
}

uintptr_t DialogHook::CalculateRelativeAddress(uintptr_t from, uintptr_t to) {
    return Rel32From(from, to);
}

bool DialogHook::PollDialogData() {
    if (!m_is_installed || m_backup_address == 0) {
        return false;
    }
    
    try {
        
        // Check if new dialog data flag is set (backup+32)
        uint8_t flag = 0;
        if (!m_memory->ReadMemory(m_backup_address + 32, &flag, 1)) {
            return false;
        }
        
        if (flag == 0) {
            return false; // No new data
        }
        
        // Read the captured register values
        uintptr_t text_ptr = 0;
        uintptr_t npc_ptr = 0;
        
        if (!m_memory->ReadMemory(m_backup_address + 16, &text_ptr, 4)) {
            return false;
        }
        
        if (!m_memory->ReadMemory(m_backup_address + 28, &npc_ptr, 4)) {
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
        if (text_address != 0) {
            m_memory->ReadString(text_address, dialog_text);
        }
        
        // Read NPC name
        std::string npc_name = "No_NPC";
        if (npc_address != 0) {
            if (!m_memory->ReadString(npc_address, npc_name) || npc_name.empty()) {
                npc_name = "No_NPC";
            }
        }
        m_last_dialog_text = dialog_text;
        m_last_npc_name = npc_name;
        
        if (m_console_output && m_console && !dialog_text.empty()) {
            m_console->PrintDialog(npc_name, dialog_text);
        }
        
        return true;
        
    } catch (...) {
        // Silently ignore errors to avoid crashes
        return false;
    }
}

} // namespace dqxclarity
