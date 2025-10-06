#include "IntegrityDetour.hpp"
#include "../pattern/PatternScanner.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"
#include "../memory/MemoryPatch.hpp"

#include <iostream>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <chrono>

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

static std::string HexDump(const uint8_t* data, size_t count, size_t bytes_per_group = 1) {
    std::ostringstream oss;
    for (size_t i = 0; i < count; ++i) {
        if (i && (i % 16 == 0)) oss << "\n";
        if (bytes_per_group > 1 && (i % bytes_per_group == 0) && (i % 16 != 0)) oss << ' ';
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(data[i]) << ' ';
    }
    return oss.str();
}

// Forward declaration for branch detection utility
static bool HasPcRelativeBranch(const uint8_t* data, size_t n);

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
    // Allocate state (1 byte) and trampoline code
    m_state_addr = m_memory->AllocateMemory(8, /*executable=*/false);
    if (m_state_addr == 0) return false;
    if (m_log.info) m_log.info("Allocated integrity state at 0x" + std::to_string((unsigned long long)m_state_addr));
    // Initialize state to 0
    uint8_t zero = 0; m_memory->WriteMemory(m_state_addr, &zero, 1);

    // Trampoline needs enough space to optionally restore several hook sites
    // Each restored byte uses 6 bytes (C6 05 <imm32> <imm8>), so allocate generously
    m_trampoline_addr = m_memory->AllocateMemory(1024, /*executable=*/true);
    if (m_trampoline_addr == 0) return false;
    if (m_log.info) m_log.info("Allocated integrity trampoline at 0x" + std::to_string((unsigned long long)m_trampoline_addr));

    // Build code: [restore hooks] ; mov byte ptr [state], 1 ; <stolen bytes> ; jmp back
    std::vector<uint8_t> code;

    // 1) Signal that integrity ran: C6 05 <state_addr> 01
    code.push_back(0xC6); code.push_back(0x05);
    uint32_t state_imm = ToImm32(m_state_addr);
    auto* p = reinterpret_cast<uint8_t*>(&state_imm);
    code.insert(code.end(), p, p + 4);
    code.push_back(0x01);


    // 3) Append stolen bytes
    bool e9_tailcall = false;
    if (!m_original_bytes.empty() && m_original_bytes[0] == 0xE9 && m_original_bytes.size() >= 5) {
        // Compute original absolute destination
        int32_t old_disp = 0; std::memcpy(&old_disp, &m_original_bytes[1], 4);
        uintptr_t orig_dest = static_cast<uintptr_t>(static_cast<int64_t>(m_integrity_addr) + 5 + static_cast<int64_t>(old_disp));
        // Emit relocated E9 from trampoline position (tail-call to original target)
        uintptr_t tramp_e9_from = m_trampoline_addr + code.size();
        code.push_back(0xE9);
        int32_t new_disp = static_cast<int32_t>(static_cast<int64_t>(orig_dest) - static_cast<int64_t>(tramp_e9_from + 5));
        auto* pd = reinterpret_cast<uint8_t*>(&new_disp);
        code.insert(code.end(), pd, pd + 4);
        // Log E9 absolute target and dump 16 bytes at target
        {
            uintptr_t base = m_memory->GetModuleBaseAddress("DQXGame.exe");
            std::ostringstream oss;
            oss << "E9Target=0x" << std::hex << orig_dest << std::dec;
            if (base) oss << " (offset +0x" << std::hex << (orig_dest - base) << ")" << std::dec;
            if (m_log.info) m_log.info(oss.str());
            uint8_t tgt[16] = {0};
            if (m_memory->ReadMemory(orig_dest, tgt, sizeof(tgt))) {
                std::ostringstream dump;
                dump << "E9Target[0..16]:\n" << HexDump(tgt, sizeof(tgt));
                if (m_log.info) m_log.info(dump.str());
            }
        }
        e9_tailcall = true;
        if (m_log.info) m_log.info("Relocated E9 in trampoline (tail-call)");
        // E9 is non-returning; do not append remaining bytes nor a return jump
    } else {
        code.insert(code.end(), m_original_bytes.begin(), m_original_bytes.end());
    }
    // nothing to remove here (cleanup of stray code)

    // 4) Return jump only if not tail-calling via E9
    uintptr_t ret_target = 0;
    if (!e9_tailcall) {
        code.push_back(0xE9);
        ret_target = m_integrity_addr + m_original_bytes.size();
        uint32_t rel = Rel32From(m_trampoline_addr + code.size() - 1, ret_target);
        auto* r = reinterpret_cast<uint8_t*>(&rel);
        code.insert(code.end(), r, r + 4);
    }

    if (!m_memory->WriteMemory(m_trampoline_addr, code.data(), code.size())) return false;
    m_memory->FlushInstructionCache(m_trampoline_addr, code.size());

    {
        // Log trampoline snippet and return target and return-site bytes
        std::ostringstream ctx;
        ctx << "Trampoline @0x" << std::hex << m_trampoline_addr << std::dec
            << ", size=" << code.size();
        if (ret_target) ctx << ", return=0x" << std::hex << ret_target << std::dec; else ctx << ", tailcall";
        if (m_log.info) m_log.info(ctx.str());
        if (m_verbose) std::cout << ctx.str() << "\n";
        LogBytes("Trampoline[0..64]", m_trampoline_addr, (std::min<size_t>)(code.size(), (size_t)64));
        if (ret_target) LogBytes("ReturnSite[0..16]", ret_target, 16);
    }

    return true;
}

bool IntegrityDetour::PatchIntegrityFunction() {
    // Write 5-byte JMP rel32 at E9 patch site
    std::vector<uint8_t> patch;
    patch.push_back(0xE9);
    uint32_t rel = Rel32From(m_integrity_addr, m_trampoline_addr);
    auto* r = reinterpret_cast<uint8_t*>(&rel);
    patch.insert(patch.end(), r, r + 4);

    {
        std::ostringstream oss;
        oss << "Integrity patch: site=0x" << std::hex << m_integrity_addr
            << " -> tramp=0x" << m_trampoline_addr << ", rel=0x" << rel << std::dec;
        if (m_log.info) m_log.info(oss.str());
        if (m_verbose) std::cout << oss.str() << "\n";
    }

    // Set RWX for patch region, write, restore RX
    if (!MemoryPatch::WriteWithProtect(*m_memory, m_integrity_addr, patch)) return false;

    // Post-patch read-back
    LogBytes("Integrity patch [0..16]", m_integrity_addr, 16);

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

    // Patch at signature head for this build (E9 gate caused login crash)

    // Log address and module offset
    {
        uintptr_t base = m_memory->GetModuleBaseAddress("DQXGame.exe");
        std::ostringstream oss;
        oss << "Integrity site=0x" << std::hex << m_integrity_addr << std::dec;
        if (base) oss << " (offset +0x" << std::hex << (m_integrity_addr - base) << ")" << std::dec;
        oss << " ; HookSite=head ; ReapplyDelayMs=2500";
        if (m_log.info) m_log.info(oss.str());
        if (m_verbose) std::cout << oss.str() << "\n";
    }

    // Pre-patch diagnostics: dump surrounding bytes (selected site)
    {
        uint8_t pre[64] = {0};
        if (m_memory->ReadMemory(m_integrity_addr, pre, sizeof(pre))) {
            std::ostringstream ctx;
            ctx << "Integrity pre-patch @0x" << std::hex << m_integrity_addr << std::dec
                << "\n" << HexDump(pre, sizeof(pre));
            if (m_log.info) m_log.info(ctx.str());
            if (m_verbose) std::cout << ctx.str() << "\n";
        }
    }

    // Choose stolen length: if E9 at site, use 7 (E9 rel32 + 6A 03); otherwise instruction-safe
    size_t stolen_len = 0;
    {
        uint8_t b0 = 0; (void)m_memory->ReadMemory(m_integrity_addr, &b0, 1);
        if (b0 == 0xE9) stolen_len = 7; else stolen_len = ComputeInstructionSafeStolenLen();
        if (stolen_len < 5) stolen_len = 8;
        if (stolen_len > 32) stolen_len = 8;
    }
    m_original_bytes.resize(stolen_len);
    if (!m_memory->ReadMemory(m_integrity_addr, m_original_bytes.data(), stolen_len)) return false;

    {
        std::ostringstream oss;
        oss << "Integrity stolen_len=" << stolen_len;
        oss << " bytes=" << HexDump(m_original_bytes.data(), m_original_bytes.size());
        // Boundary info: next byte after stolen_len (prove we stop pre-E9)
        uint8_t nextb = 0;
        if (m_memory->ReadMemory(m_integrity_addr + stolen_len, &nextb, 1)) {
            oss << " boundary_next=" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)nextb;
            if (nextb == 0xE9) oss << "(E9)";
            oss << std::dec;
        }
        if (HasPcRelativeBranch(m_original_bytes.data(), m_original_bytes.size())) {
            oss << " [pc-relative branch present]";
        }
        if (m_log.info) m_log.info(oss.str());
        if (m_verbose) std::cout << oss.str() << "\n";
    }

    if (m_verbose) {
        std::cout << "Integrity patch site at 0x" << std::hex << m_integrity_addr << std::dec << "\n";
        std::cout << "Original[0..16]: "; for (auto b : m_original_bytes) std::printf("%02X ", b); std::cout << "\n";
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

size_t IntegrityDetour::DecodeInstrLen(const uint8_t* p, size_t max) {
    if (max == 0) return 0;
    const uint8_t op = p[0];

    // Simple coverage for the opcodes we observe in the integrity signature
    // - 0xE9: jmp rel32 (5 bytes)
    // - 0x6A: push imm8 (2 bytes)
    // - 0x89: mov r/m32, r32 (with ModRM/SIB/disp handling)
    // - 0x8B: mov r32, r/m32 (not observed in the signature start but safe to include)
    // - 0x8D: lea r32, m (with ModRM/SIB/disp handling)

    if (op == 0xE9) {
        return (max >= 5) ? 5 : 0;
    }
    if (op == 0x6A) {
        return (max >= 2) ? 2 : 0;
    }

    auto decode_modrm = [&](size_t i)->size_t {
        if (i >= max) return 0;
        uint8_t modrm = p[i++];
        uint8_t mod = (modrm >> 6) & 0x3;
        uint8_t rm  = (modrm & 0x7);
        // SIB present when mod != 3 and rm == 4
        if (mod != 3 && rm == 4) {
            if (i >= max) return 0; // SIB
            uint8_t sib = p[i++];
            (void)sib;
        }
        if (mod == 1) { // disp8
            if (i + 1 > max) return 0; else i += 1;
        } else if (mod == 2) { // disp32
            if (i + 4 > max) return 0; else i += 4;
        } else if (mod == 0 && rm == 5) { // disp32 (no base)
            if (i + 4 > max) return 0; else i += 4;
        }
        return i;
    };

    switch (op) {
        case 0x89: // mov r/m32, r32
        case 0x8B: // mov r32, r/m32
        case 0x8D: { // lea r32, m
            size_t i = 1; // after opcode
            size_t res = decode_modrm(i);
            return (res > 0) ? res : 0;
        }
        default:
            // Unknown opcode: return 0 to signal failure
            return 0;
    }
}

static bool HasPcRelativeBranch(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9 || b == 0xEB) return true; // call/jmp
        if (b == 0x0F && i + 1 < n) {
            uint8_t b2 = data[i + 1];
            if ((b2 & 0xF0) == 0x80) return true; // 0F 8x
        }
    }
    return false;
}

size_t IntegrityDetour::ComputeInstructionSafeStolenLen() {
    // Read a small window from the integrity site
    uint8_t buf[32] = {0};
    if (!m_memory->ReadMemory(m_integrity_addr, buf, sizeof(buf))) {
        return 8; // fallback
    }

    size_t off = 0;
    for (int insn = 0; insn < 16 && off < sizeof(buf); ++insn) {
        // Stop before the first E9 (jmp rel32) if we've already covered >=5 bytes.
        if (off >= 5 && buf[off] == 0xE9) {
            return off; // stop just before E9; avoids relocating mid-stream
        }
        size_t len = DecodeInstrLen(buf + off, sizeof(buf) - off);
        if (len == 0) break; // decoding failed
        off += len;
        if (off >= 5 && off < sizeof(buf)) {
            // If next byte starts an E9, we will stop in the next loop iteration; otherwise continue
            // Still, if we've covered plenty (>=12) and no E9 seen, return to avoid overreach
            if (off >= 12 && buf[off] != 0xE9) return off;
        }
    }
    // If we got here, prefer 8 as conservative
    return (off >= 5) ? off : 8;
}


} // namespace dqxclarity
