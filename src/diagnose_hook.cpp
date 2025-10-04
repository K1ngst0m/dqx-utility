#include "dqxclarity/memory/MemoryFactory.hpp"
#include "dqxclarity/memory/IProcessMemory.hpp"
#include "dqxclarity/signatures/Signatures.hpp"
#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <iomanip>

using namespace dqxclarity;

DWORD FindProcessByName(const std::string& name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &entry)) {
        do {
            if (name == entry.szExeFile) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return 0;
}

int main() {
    std::cout << "=== DQX Hook Location Diagnostic Tool ===\n\n";
    
    // Find DQXGame process
    DWORD pid = FindProcessByName("DQXGame.exe");
    if (pid == 0) {
        std::cerr << "ERROR: DQXGame.exe not found!\n";
        return 1;
    }
    
    std::cout << "Found DQXGame.exe with PID: " << pid << "\n\n";
    
    // Attach to process
    auto memory_unique = MemoryFactory::CreatePlatformMemory();
    auto memory = std::shared_ptr<IProcessMemory>(std::move(memory_unique));
    if (!memory || !memory->AttachProcess(pid)) {
        std::cerr << "ERROR: Failed to attach to process!\n";
        return 1;
    }
    
    std::cout << "Successfully attached to process\n\n";
    
    // Get base address
    uintptr_t base = memory->GetModuleBaseAddress("DQXGame.exe");
    if (base == 0) {
        std::cerr << "ERROR: Failed to get DQXGame.exe base address!\n";
        return 1;
    }
    
    std::cout << "DQXGame.exe base address: 0x" << std::hex << base << std::dec << "\n\n";
    
    // Get pattern
    auto pattern = Signatures::GetDialogTrigger();
    std::cout << "Searching for pattern: ";
    for (size_t i = 0; i < pattern.Size(); ++i) {
        if (pattern.mask[i]) {
            printf("%02X ", pattern.bytes[i]);
        } else {
            printf("?? ");
        }
    }
    std::cout << "\n\n";
    
    // Scan for pattern
    const size_t scan_size = 50 * 1024 * 1024;
    const size_t chunk_size = 64 * 1024;
    std::vector<uint8_t> buffer(chunk_size);
    
    uintptr_t found_address = 0;
    for (uintptr_t addr = base; addr < base + scan_size; addr += chunk_size) {
        if (!memory->ReadMemory(addr, buffer.data(), chunk_size)) {
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
                found_address = addr + i;
                break;
            }
        }
        if (found_address != 0) break;
    }
    
    if (found_address == 0) {
        std::cerr << "ERROR: Pattern not found!\n";
        return 1;
    }
    
    std::cout << "FOUND PATTERN AT: 0x" << std::hex << found_address << std::dec << "\n";
    std::cout << "Offset from base: 0x" << std::hex << (found_address - base) << std::dec << "\n\n";
    
    // Read 50 bytes at the location
    std::vector<uint8_t> bytes(50);
    if (!memory->ReadMemory(found_address, bytes.data(), 50)) {
        std::cerr << "ERROR: Failed to read bytes at hook location!\n";
        return 1;
    }
    
    std::cout << "=== BYTES AT HOOK LOCATION ===\n";
    std::cout << "Address: 0x" << std::hex << found_address << std::dec << "\n\n";
    
    // Display bytes in hex dump format
    for (size_t i = 0; i < 50; i += 16) {
        printf("0x%08llX: ", found_address + i);
        
        // Hex bytes
        for (size_t j = 0; j < 16 && (i + j) < 50; ++j) {
            printf("%02X ", bytes[i + j]);
        }
        
        // Padding
        for (size_t j = (i + 16 < 50 ? 16 : 50 - i); j < 16; ++j) {
            printf("   ");
        }
        
        // ASCII representation
        printf(" | ");
        for (size_t j = 0; j < 16 && (i + j) < 50; ++j) {
            char c = bytes[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        
        printf("\n");
    }
    
    std::cout << "\n=== FIRST 10 BYTES ANALYSIS ===\n";
    std::cout << "These are the bytes we would need to 'steal' for a 10-byte hook:\n";
    for (size_t i = 0; i < 10; ++i) {
        printf("Byte %zu: 0x%02X\n", i, bytes[i]);
    }
    
    std::cout << "\n=== DIAGNOSTIC COMPLETE ===\n";
    std::cout << "The program did NOT modify any memory.\n";
    std::cout << "The game should continue running normally.\n";
    
    memory->DetachProcess();
    return 0;
}
