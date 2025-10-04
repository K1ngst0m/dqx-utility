#include "dqxclarity/memory/MemoryFactory.hpp"
#include "dqxclarity/memory/IProcessMemory.hpp"
#include "dqxclarity/process/ProcessFinder.hpp"
#include "dqxclarity/hooking/DialogHook.hpp"
#include "dqxclarity/signatures/Signatures.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <string>
#include <cstdint>

using namespace dqxclarity;

volatile bool g_running = true;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n\nReceived interrupt signal. Shutting down...\n";
        g_running = false;
    }
}

static uint32_t calc_rel32_from_e9(uintptr_t from_e9, uintptr_t dest) {
    // Matches C++ CalculateRelativeAddress: disp = dest - (from + 5)
    int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(from_e9 + 5);
    return static_cast<uint32_t>(diff);
}

static void hexdump_line(uintptr_t addr, const std::vector<uint8_t>& bytes, size_t start, size_t len) {
    std::cout << "0x" << std::setfill('0') << std::setw(8) << std::hex << addr << ": ";
    for (size_t i = 0; i < len; ++i) {
        if (start + i < bytes.size()) {
            std::cout << std::uppercase << std::setw(2) << std::setfill('0') << (int)bytes[start + i] << " ";
        }
    }
    std::cout << std::dec << "\n";
}

static void print_bytes(const char* label, const std::vector<uint8_t>& v) {
    std::cout << label << " (" << v.size() << " bytes): ";
    for (auto b : v) std::cout << std::uppercase << std::setw(2) << std::setfill('0') << std::hex << (int)b << " ";
    std::cout << std::dec << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "DQXClarity C++ Dialog Hook Test\n";
    std::cout << "================================\n\n";

    bool install = false; // default: diagnostic-only
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--install" || arg == "-i") {
            install = true;
        }
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Find DQXGame.exe
    std::cout << "[1/4] Looking for DQXGame.exe...\n";
    auto pids = ProcessFinder::FindByName("DQXGame.exe", false);
    if (pids.empty()) {
        std::cerr << "ERROR: DQXGame.exe not found!\n";
        std::cerr << "Make sure the game is running.\n";
        return 1;
    }
    std::cout << "  Found PID: " << pids[0] << "\n\n";
    
    // Create memory interface
    std::cout << "[2/4] Attaching to process...\n";
    auto memory_unique = MemoryFactory::CreatePlatformMemory();
    auto memory = std::shared_ptr<IProcessMemory>(std::move(memory_unique));
    
    if (!memory || !memory->AttachProcess(pids[0])) {
        std::cerr << "ERROR: Failed to attach to process!\n";
        std::cerr << "Make sure you're running as Administrator.\n";
        return 1;
    }
    std::cout << "  Attached successfully\n\n";

    // Diagnostic-only by default: find pattern and dump bytes
    auto windows_memory = memory;
    uintptr_t base = windows_memory->GetModuleBaseAddress("DQXGame.exe");
    auto pattern = Signatures::GetDialogTrigger();
    if (base == 0) {
        std::cerr << "ERROR: Failed to get DQXGame.exe base address\n";
        return 1;
    }

    // Scan memory for pattern (same as DialogHook::FindDialogTriggerAddress)
    uintptr_t hook_addr = 0;
    const size_t scan_size = 50 * 1024 * 1024;
    const size_t chunk_size = 64 * 1024;
    std::vector<uint8_t> buffer(chunk_size);
    for (uintptr_t addr = base; addr < base + scan_size; addr += chunk_size) {
        if (!memory->ReadMemory(addr, buffer.data(), chunk_size)) continue;
        for (size_t i = 0; i <= chunk_size - pattern.Size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < pattern.Size(); ++j) {
                if (!pattern.mask[j]) continue;
                if (buffer[i + j] != pattern.bytes[j]) { match = false; break; }
            }
            if (match) { hook_addr = addr + i; break; }
        }
        if (hook_addr) break;
    }

    if (!hook_addr) {
        std::cerr << "ERROR: Pattern not found in first " << (scan_size / 1024 / 1024) << " MB\n";
        return 1;
    }

    std::cout << "Dialog trigger found at: 0x" << std::hex << hook_addr << std::dec << " (offset 0x" << std::hex << (hook_addr - base) << std::dec << ")\n";

    // Read first 64 bytes at hook
    std::vector<uint8_t> ahead(64);
    memory->ReadMemory(hook_addr, ahead.data(), ahead.size());
    std::cout << "\nBytes at hook location (first 64):\n";
    hexdump_line(hook_addr + 0, ahead, 0, 16);
    hexdump_line(hook_addr + 16, ahead, 16, 16);
    hexdump_line(hook_addr + 32, ahead, 32, 16);
    hexdump_line(hook_addr + 48, ahead, 48, 16);

    const size_t stolen = 10; // Python uses 10 here (3 + 7)
    std::vector<uint8_t> stolen_bytes(stolen);
    if (!memory->ReadMemory(hook_addr, stolen_bytes.data(), stolen)) {
        std::cerr << "ERROR: Failed to read stolen bytes\n";
        return 1;
    }
    print_bytes("Stolen bytes", stolen_bytes);

    if (!install) {
        std::cout << "\nDiagnostic-only mode (no writes).\n";
        std::cout << "Constructing Python-style trampoline bytes in-memory...\n";

        // Allocate memory regions to compute REAL displacements but do not write code
        uintptr_t mov_insts_addr = memory->AllocateMemory(150, true); // executable, like Python
        uintptr_t backup_addr    = memory->AllocateMemory(50, false);
        uintptr_t shellcode_addr = memory->AllocateMemory(2048, false);

        if (!mov_insts_addr || !backup_addr || !shellcode_addr) {
            std::cerr << "ERROR: Failed to allocate diagnostic memory blocks\n";
            if (mov_insts_addr) memory->FreeMemory(mov_insts_addr, 150);
            if (backup_addr)    memory->FreeMemory(backup_addr, 50);
            if (shellcode_addr) memory->FreeMemory(shellcode_addr, 2048);
            return 1;
        }

        std::cout << std::hex;
        std::cout << "mov_insts_addr: 0x" << mov_insts_addr << ", backup_addr: 0x" << backup_addr << ", shellcode_addr: 0x" << shellcode_addr << "\n";
        std::cout << std::dec;

        std::vector<uint8_t> detour;
        auto push_u32 = [&](uint32_t v){
            detour.push_back(static_cast<uint8_t>(v & 0xFF));
            detour.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            detour.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            detour.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        };

        // Save registers (match Python)
        detour.push_back(0xA3); push_u32(static_cast<uint32_t>(backup_addr)); detour.push_back(0x90);
        detour.insert(detour.end(), {0x89,0x1D}); push_u32(static_cast<uint32_t>(backup_addr + 4));
        detour.insert(detour.end(), {0x89,0x0D}); push_u32(static_cast<uint32_t>(backup_addr + 8));
        detour.insert(detour.end(), {0x89,0x15}); push_u32(static_cast<uint32_t>(backup_addr + 12));
        detour.insert(detour.end(), {0x89,0x35}); push_u32(static_cast<uint32_t>(backup_addr + 16));
        detour.insert(detour.end(), {0x89,0x3D}); push_u32(static_cast<uint32_t>(backup_addr + 20));
        detour.insert(detour.end(), {0x89,0x2D}); push_u32(static_cast<uint32_t>(backup_addr + 24));
        detour.insert(detour.end(), {0x89,0x25}); push_u32(static_cast<uint32_t>(backup_addr + 28));

        // push shellcode addr
        detour.push_back(0x68); push_u32(static_cast<uint32_t>(shellcode_addr));
        // call PyRun_SimpleString - placeholder rel32 (we're not injecting Python here)
        detour.push_back(0xE8); push_u32(0u);

        // Restore registers
        detour.push_back(0xA1); push_u32(static_cast<uint32_t>(backup_addr)); detour.push_back(0x90);
        detour.insert(detour.end(), {0x8B,0x1D}); push_u32(static_cast<uint32_t>(backup_addr + 4));
        detour.insert(detour.end(), {0x8B,0x0D}); push_u32(static_cast<uint32_t>(backup_addr + 8));
        detour.insert(detour.end(), {0x8B,0x15}); push_u32(static_cast<uint32_t>(backup_addr + 12));
        detour.insert(detour.end(), {0x8B,0x35}); push_u32(static_cast<uint32_t>(backup_addr + 16));
        detour.insert(detour.end(), {0x8B,0x3D}); push_u32(static_cast<uint32_t>(backup_addr + 20));
        detour.insert(detour.end(), {0x8B,0x2D}); push_u32(static_cast<uint32_t>(backup_addr + 24));
        detour.insert(detour.end(), {0x8B,0x25}); push_u32(static_cast<uint32_t>(backup_addr + 28));

        // Append stolen bytes
        detour.insert(detour.end(), stolen_bytes.begin(), stolen_bytes.end());

        // Compute return jump E9 from detour back to hook + stolen
        uintptr_t jmp_from = mov_insts_addr + detour.size(); // address of E9 opcode in detour
        uintptr_t jmp_dest = hook_addr + stolen;
        uint32_t rel_back = calc_rel32_from_e9(jmp_from, jmp_dest);
        detour.push_back(0xE9);
        push_u32(rel_back);

        print_bytes("Detour bytes (Python-style, placeholder E8 rel32=00000000)", detour);

        // Compute original patch E9 (hook -> detour)
        std::vector<uint8_t> patch;
        patch.push_back(0xE9);
        uint32_t rel_patch = calc_rel32_from_e9(hook_addr, mov_insts_addr);
        auto push_u32_patch = [&](uint32_t v){
            patch.push_back(static_cast<uint8_t>(v & 0xFF));
            patch.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            patch.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            patch.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        };
        push_u32_patch(rel_patch);
        while (patch.size() < stolen) patch.push_back(0x90);
        print_bytes("Patch bytes for hook (E9 + NOPs)", patch);

        std::cout << "\nNo changes were written to the process. Freeing temporary memory...\n";
        memory->FreeMemory(mov_insts_addr, 150);
        memory->FreeMemory(backup_addr, 50);
        memory->FreeMemory(shellcode_addr, 2048);

        std::cout << "\nDiagnostics complete.\n";
        return 0;
    }

    // If explicitly requested, install the hook (same as before)
    std::cout << "[3/4] Installing dialog hook...\n";
    auto hook = std::make_unique<DialogHook>(memory);
    hook->SetSafeMode(false); // enable full detour (register capture + flag)
    if (!hook->InstallHook()) {
        std::cerr << "ERROR: Failed to install dialog hook!\n";
        return 1;
    }
    std::cout << "  Hook installed successfully\n\n";

    // Monitor for dialog
    std::cout << "[4/4] Monitoring for dialog...\n";
    std::cout << "==========================================\n";
    std::cout << "Waiting for in-game dialog to appear...\n";
    std::cout << "Press Ctrl+C to exit.\n";
    std::cout << "==========================================\n\n";

    int dialog_count = 0;
    while (g_running) {
        if (hook->PollDialogData()) {
            dialog_count++;
            std::cout << "\n[Dialog #" << dialog_count << " captured]\n";
            std::cout << "Text: " << hook->GetLastDialogText() << "\n";
            std::cout << "NPC: " << hook->GetLastNpcName() << "\n";
            std::cout << "==========================================\n\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nRemoving hook and cleaning up...\n";
    hook->RemoveHook();
    std::cout << "\nDialog Hook Test completed.\n";
    std::cout << "Total dialogs captured: " << dialog_count << "\n";
    return 0;
}
