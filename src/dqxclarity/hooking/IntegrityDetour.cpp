#include "IntegrityDetour.hpp"
#include "../pattern/PatternScanner.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"
#include "../memory/MemoryPatch.hpp"

#include <iostream>
#include <cstdio>

namespace dqxclarity {

IntegrityDetour::IntegrityDetour(std::shared_ptr<IProcessMemory> memory)
    : m_memory(std::move(memory)) {}

IntegrityDetour::~IntegrityDetour() {
    // Do not automatically remove; Engine controls lifecycle
}

bool IntegrityDetour::FindIntegrityAddress(uintptr_t& out_addr) {
    PatternScanner scanner(m_memory);
    auto pat = Signatures::GetIntegrityCheck();

    // Prefer scanning the module
    if (auto mod = scanner.ScanModule(pat, "DQXGame.exe")) {
        out_addr = *mod;
        return true;
    }
    // Fallback: process-wide executable regions
    if (auto any = scanner.ScanProcess(pat, /*require_executable=*/true)) {
        out_addr = *any;
        return true;
    }
    return false;
}

void IntegrityDetour::LogBytes(const char* label, uintptr_t addr, size_t count) {
    if (!m_verbose) return;
    std::vector<uint8_t> buf(count);
    if (m_memory->ReadMemory(addr, buf.data(), buf.size())) {
        std::cout << label << " @0x" << std::hex << addr << std::dec << ": ";
        for (auto b : buf) std::printf("%02X ", b);
        std::cout << "\n";
    }
}

bool IntegrityDetour::BuildAndWriteTrampoline() {
    // Allocate state (1 byte) and trampoline code (64 bytes should be enough)
    m_state_addr = m_memory->AllocateMemory(8, /*executable=*/false);
    if (m_state_addr == 0) return false;
    if (m_log.info) m_log.info("Allocated integrity state at 0x" + std::to_string((unsigned long long)m_state_addr));
    // Initialize state to 0
    uint8_t zero = 0; m_memory->WriteMemory(m_state_addr, &zero, 1);

    m_trampoline_addr = m_memory->AllocateMemory(128, /*executable=*/true);
    if (m_trampoline_addr == 0) return false;
    if (m_log.info) m_log.info("Allocated integrity trampoline at 0x" + std::to_string((unsigned long long)m_trampoline_addr));

    // Build code: mov byte ptr [state], 1 ; <stolen bytes> ; jmp back
    std::vector<uint8_t> code;
    // C6 05 <imm32 addr> 01
    code.push_back(0xC6); code.push_back(0x05);
    uint32_t state_imm = ToImm32(m_state_addr);
    auto* p = reinterpret_cast<uint8_t*>(&state_imm);
    code.insert(code.end(), p, p + 4);
    code.push_back(0x01);

    // Append stolen bytes
    code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());

    // Jump back to integrity_addr + stolen_len
    code.push_back(0xE9);
    uint32_t rel = Rel32From(m_trampoline_addr + code.size() - 1, m_integrity_addr + m_original_bytes.size());
    auto* r = reinterpret_cast<uint8_t*>(&rel);
    code.insert(code.end(), r, r + 4);

    if (!m_memory->WriteMemory(m_trampoline_addr, code.data(), code.size())) return false;
    m_memory->FlushInstructionCache(m_trampoline_addr, code.size());

    if (m_verbose) {
        std::cout << "Integrity trampoline written at 0x" << std::hex << m_trampoline_addr << std::dec
                  << ", size=" << code.size() << " bytes\n";
        LogBytes("Trampoline[0..32]", m_trampoline_addr, (std::min<size_t>)(code.size(), 32));
    }
    if (m_log.info) m_log.info("Integrity trampoline written at 0x" + std::to_string((unsigned long long)m_trampoline_addr));

    return true;
}

bool IntegrityDetour::PatchIntegrityFunction() {
    // Write JMP rel32 and pad with NOPs up to stolen bytes length
    std::vector<uint8_t> patch;
    patch.push_back(0xE9);
    uint32_t rel = Rel32From(m_integrity_addr, m_trampoline_addr);
    auto* r = reinterpret_cast<uint8_t*>(&rel);
    patch.insert(patch.end(), r, r + 4);
    while (patch.size() < m_original_bytes.size()) patch.push_back(0x90);

    if (m_verbose) {
        std::cout << "Patching integrity at 0x" << std::hex << m_integrity_addr << std::dec
                  << " with JMP -> 0x" << std::hex << m_trampoline_addr << std::dec
                  << ", rel=0x" << std::hex << rel << std::dec << "\n";
    }
    if (m_log.info) m_log.info("Patching integrity with JMP");

    // Set RWX for patch region, write, restore RX
    if (!MemoryPatch::WriteWithProtect(*m_memory, m_integrity_addr, patch)) return false;

    if (m_verbose) {
        LogBytes("Integrity patch bytes", m_integrity_addr, (std::max<size_t>)(m_original_bytes.size(), (size_t)8));
    }
    if (m_log.info) m_log.info("Integrity patch applied");

    return true;
}

bool IntegrityDetour::Install() {
    if (m_installed) return true;
    if (!m_memory || !m_memory->IsProcessAttached()) return false;

    if (!FindIntegrityAddress(m_integrity_addr)) {
        if (m_verbose) std::cout << "Integrity pattern not found\n";
        if (m_log.error) m_log.error("Integrity pattern not found");
        return false;
    }

    // Read stolen bytes (8 bytes like Python)
    const size_t stolen_len = 8;
    m_original_bytes.resize(stolen_len);
    if (!m_memory->ReadMemory(m_integrity_addr, m_original_bytes.data(), stolen_len)) return false;

    if (m_verbose) {
        std::cout << "Integrity found at 0x" << std::hex << m_integrity_addr << std::dec << "\n";
        std::cout << "Original[0..8]: "; for (auto b : m_original_bytes) std::printf("%02X ", b); std::cout << "\n";
    }
    if (m_log.info) {
        m_log.info("Integrity found at 0x" + std::to_string((unsigned long long)m_integrity_addr));
    }

    // Verify no changes just before patch
    {
        std::vector<uint8_t> cur(stolen_len);
        if (m_memory->ReadMemory(m_integrity_addr, cur.data(), cur.size())) {
            if (cur != m_original_bytes) {
                if (m_log.warn) m_log.warn("Integrity site bytes changed before patch; refreshing snapshot");
                m_original_bytes = cur;
            }
        }
    }

    if (!BuildAndWriteTrampoline()) return false;

    if (!PatchIntegrityFunction()) return false;

    m_installed = true;
    return true;
}

void IntegrityDetour::Remove() {
    if (!m_installed) return;
    if (m_integrity_addr != 0 && !m_original_bytes.empty()) {
        // restore original bytes
        (void)m_memory->WriteMemory(m_integrity_addr, m_original_bytes.data(), m_original_bytes.size());
        m_memory->FlushInstructionCache(m_integrity_addr, m_original_bytes.size());
        if (m_verbose) LogBytes("Integrity restored", m_integrity_addr, (std::max<size_t>)(m_original_bytes.size(), (size_t)8));
    }
    if (m_trampoline_addr) {
        (void)m_memory->FreeMemory(m_trampoline_addr, 128);
        m_trampoline_addr = 0;
    }
    if (m_state_addr) {
        (void)m_memory->FreeMemory(m_state_addr, 8);
        m_state_addr = 0;
    }
    m_original_bytes.clear();
    m_installed = false;
}

} // namespace dqxclarity