#include "IntegrityHook.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"
#include "../util/Profile.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace dqxclarity
{

IntegrityHook::IntegrityHook(const HookCreateInfo& create_info)
    : HookBase(create_info)
{
}

IntegrityHook::~IntegrityHook()
{
    // Cleanup handled by HookBase
}

Pattern IntegrityHook::GetSignature() const
{
    return Signatures::GetIntegrityCheck();
}

std::vector<uint8_t> IntegrityHook::GenerateDetourPayload()
{
    PROFILE_SCOPE_FUNCTION();

    // Allocate state flag (1 byte) for integrity signal
    state_address_ = memory()->AllocateMemory(8, /*executable=*/false);
    if (state_address_ == 0)
    {
        if (logger().error)
            logger().error("Failed to allocate integrity state memory");
        return {};
    }

    // Initialize state to 0
    uint8_t zero = 0;
    if (!memory()->WriteMemory(state_address_, &zero, 1))
    {
        if (logger().error)
            logger().error("Failed to initialize integrity state");
        return {};
    }

    if (logger().info)
    {
        std::ostringstream oss;
        oss << "Allocated integrity state at 0x" << std::hex << state_address_ << std::dec;
        logger().info(oss.str());
    }

    // Build trampoline: signal state → stolen bytes → return jump
    std::vector<uint8_t> code;

    // Signal that integrity check ran: mov byte ptr [state_address_], 1
    // Encoding: C6 05 [imm32] 01
    code.push_back(0xC6);
    code.push_back(0x05);
    uint32_t state_imm = ToImm32(state_address_);
    auto* p = reinterpret_cast<uint8_t*>(&state_imm);
    code.insert(code.end(), p, p + 4);
    code.push_back(0x01);

    // Append stolen bytes (with special handling for E9 tail-call)
    bool e9_tailcall = false;
    const auto& stolen = stolen_bytes();

    if (!stolen.empty() && stolen[0] == 0xE9 && stolen.size() >= 5)
    {
        // E9 is a relative jump - need to relocate it
        int32_t old_disp = 0;
        std::memcpy(&old_disp, &stolen[1], 4);
        
        uintptr_t orig_dest = static_cast<uintptr_t>(
            static_cast<int64_t>(hook_address()) + 5 + static_cast<int64_t>(old_disp));
        
        // Emit relocated E9 from detour position
        uintptr_t detour_e9_pos = detour_address() + code.size();
        code.push_back(0xE9);
        
        int32_t new_disp = static_cast<int32_t>(
            static_cast<int64_t>(orig_dest) - static_cast<int64_t>(detour_e9_pos + 5));
        
        auto* pd = reinterpret_cast<uint8_t*>(&new_disp);
        code.insert(code.end(), pd, pd + 4);

        e9_tailcall = true;

        if (logger().debug)
        {
            uintptr_t base = memory()->GetModuleBaseAddress("DQXGame.exe");
            std::ostringstream oss;
            oss << "Relocated E9 in integrity trampoline (tail-call to 0x" 
                << std::hex << orig_dest << std::dec << ")";
            if (base)
                oss << " (offset +0x" << std::hex << (orig_dest - base) << std::dec << ")";
            logger().debug(oss.str());
        }
    }
    else
    {
        // Standard case: copy stolen bytes as-is
        code.insert(code.end(), stolen.begin(), stolen.end());
    }

    // Return jump (only if not tail-calling via E9)
    if (!e9_tailcall)
    {
        code.push_back(0xE9);
        uintptr_t ret_target = hook_address() + stolen.size();
        uint32_t rel = Rel32From(detour_address() + code.size() - 1, ret_target);
        auto* r = reinterpret_cast<uint8_t*>(&rel);
        code.insert(code.end(), r, r + 4);

        if (logger().debug)
        {
            std::ostringstream oss;
            oss << "Integrity trampoline return to 0x" << std::hex << ret_target << std::dec;
            logger().debug(oss.str());
        }
    }

    return code;
}

size_t IntegrityHook::ComputeStolenLength()
{
    // Special case: if first byte is E9 (jmp rel32), steal 7 bytes
    uint8_t first_byte = 0;
    if (memory()->ReadMemory(hook_address(), &first_byte, 1) && first_byte == 0xE9)
    {
        return 7;
    }

    // Otherwise, use instruction-safe decoding to find boundary before next E9
    uint8_t buf[32] = {0};
    if (!memory()->ReadMemory(hook_address(), buf, sizeof(buf)))
    {
        return 8; // Fallback
    }

    size_t offset = 0;
    for (int insn = 0; insn < 16 && offset < sizeof(buf); ++insn)
    {
        // Stop before E9 if we've covered at least 5 bytes
        if (offset >= 5 && buf[offset] == 0xE9)
        {
            return offset;
        }

        size_t len = DecodeInstrLen(buf + offset, sizeof(buf) - offset);
        if (len == 0)
            break; // Decoding failed

        offset += len;

        // Conservative limit: stop at 12 bytes if no E9 ahead
        if (offset >= 12 && buf[offset] != 0xE9)
        {
            return offset;
        }
    }

    return (offset >= 5) ? offset : 8;
}

void IntegrityHook::AddRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes)
{
    std::lock_guard<std::mutex> lock(restore_mutex_);
    for (auto& site : restore_sites_)
    {
        if (site.address == address)
        {
            site.bytes = original_bytes;
            return;
        }
    }
    restore_sites_.push_back({address, original_bytes});
}

void IntegrityHook::UpdateRestoreTarget(uintptr_t address, const std::vector<uint8_t>& original_bytes)
{
    AddRestoreTarget(address, original_bytes);
}

void IntegrityHook::MoveRestoreTarget(uintptr_t old_address, uintptr_t new_address, 
                                      const std::vector<uint8_t>& original_bytes)
{
    std::lock_guard<std::mutex> lock(restore_mutex_);
    for (auto& site : restore_sites_)
    {
        if (site.address == old_address)
        {
            site.address = new_address;
            site.bytes = original_bytes;
            return;
        }
    }
    restore_sites_.push_back({new_address, original_bytes});
}

std::vector<IntegrityHook::RestoreSite> IntegrityHook::GetRestoreSites() const
{
    std::lock_guard<std::mutex> lock(restore_mutex_);
    return restore_sites_;
}

// Simple x86 instruction decoder for integrity hook boundary detection
size_t IntegrityHook::DecodeInstrLen(const uint8_t* p, size_t max)
{
    if (max == 0)
        return 0;

    const uint8_t op = p[0];

    // E9: jmp rel32 (5 bytes)
    if (op == 0xE9)
        return (max >= 5) ? 5 : 0;

    // 6A: push imm8 (2 bytes)
    if (op == 0x6A)
        return (max >= 2) ? 2 : 0;

    // ModR/M decoder for 89/8B/8D opcodes
    auto decode_modrm = [&](size_t i) -> size_t
    {
        if (i >= max)
            return 0;
        
        uint8_t modrm = p[i++];
        uint8_t mod = (modrm >> 6) & 0x3;
        uint8_t rm = (modrm & 0x7);

        // SIB byte present when mod != 3 and rm == 4
        if (mod != 3 && rm == 4)
        {
            if (i >= max)
                return 0;
            ++i; // Skip SIB
        }

        // Displacement
        if (mod == 1)
        {
            // disp8
            if (i + 1 > max)
                return 0;
            i += 1;
        }
        else if (mod == 2)
        {
            // disp32
            if (i + 4 > max)
                return 0;
            i += 4;
        }
        else if (mod == 0 && rm == 5)
        {
            // disp32 (no base)
            if (i + 4 > max)
                return 0;
            i += 4;
        }

        return i;
    };

    switch (op)
    {
    case 0x89: // mov r/m32, r32
    case 0x8B: // mov r32, r/m32
    case 0x8D: // lea r32, m
    {
        size_t result = decode_modrm(1);
        return (result > 0) ? result : 0;
    }
    default:
        return 0; // Unknown opcode
    }
}

bool IntegrityHook::HasPcRelativeBranch(const uint8_t* data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        uint8_t b = data[i];
        
        // E8/E9/EB: call/jmp
        if (b == 0xE8 || b == 0xE9 || b == 0xEB)
            return true;
        
        // 0F 8x: conditional jumps
        if (b == 0x0F && i + 1 < n)
        {
            uint8_t b2 = data[i + 1];
            if ((b2 & 0xF0) == 0x80)
                return true;
        }
    }
    return false;
}

} // namespace dqxclarity

