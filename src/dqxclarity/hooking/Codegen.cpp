#include "Codegen.hpp"
#include <cstring>

namespace dqxclarity
{

// x86 Instruction Opcodes (32-bit)
namespace x86
{
// MOV register to memory [addr]
constexpr uint8_t MOV_EAX_TO_MOFFS32 = 0xA3; // mov [addr], eax
constexpr uint8_t MOV_RM32_TO_R32 = 0x89; // mov [addr], reg (requires ModR/M)

// MOV memory to register
constexpr uint8_t MOV_MOFFS32_TO_EAX = 0xA1; // mov eax, [addr]
constexpr uint8_t MOV_R32_TO_RM32 = 0x8B; // mov reg, [addr] (requires ModR/M)

// MOV immediate byte to memory
constexpr uint8_t MOV_IMM8_TO_RM8 = 0xC6; // mov byte ptr [addr], imm8

// Jump
constexpr uint8_t JMP_REL32 = 0xE9; // jmp rel32

// NOP
constexpr uint8_t NOP = 0x90;

// ModR/M bytes for [disp32] addressing (Mod=00, R/M=101)
namespace ModRM
{
constexpr uint8_t EAX_DISP32 = 0x05; // [disp32] with EAX
constexpr uint8_t EBX_DISP32 = 0x1D; // [disp32] with EBX
constexpr uint8_t ECX_DISP32 = 0x0D; // [disp32] with ECX
constexpr uint8_t EDX_DISP32 = 0x15; // [disp32] with EDX
constexpr uint8_t ESI_DISP32 = 0x35; // [disp32] with ESI
constexpr uint8_t EDI_DISP32 = 0x3D; // [disp32] with EDI
constexpr uint8_t EBP_DISP32 = 0x2D; // [disp32] with EBP
constexpr uint8_t ESP_DISP32 = 0x25; // [disp32] with ESP
constexpr uint8_t MEM_DISP32 = 0x05; // [disp32] for MOV byte ptr
} // namespace ModRM
} // namespace x86

void X86CodeBuilder::emitU32(uint32_t value)
{
    code_.insert(code_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 4);
}

void X86CodeBuilder::movToMem(Register reg, uint32_t addr)
{
    switch (reg)
    {
    case Register::EAX:
        // mov [addr], eax (A3 opcode is special form for EAX)
        code_.push_back(x86::MOV_EAX_TO_MOFFS32);
        emitU32(addr);
        code_.push_back(x86::NOP); // Alignment NOP from original code
        break;
    case Register::EBX:
        // mov [addr], ebx
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::EBX_DISP32);
        emitU32(addr);
        break;
    case Register::ECX:
        // mov [addr], ecx
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::ECX_DISP32);
        emitU32(addr);
        break;
    case Register::EDX:
        // mov [addr], edx
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::EDX_DISP32);
        emitU32(addr);
        break;
    case Register::ESI:
        // mov [addr], esi
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::ESI_DISP32);
        emitU32(addr);
        break;
    case Register::EDI:
        // mov [addr], edi
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::EDI_DISP32);
        emitU32(addr);
        break;
    case Register::EBP:
        // mov [addr], ebp
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::EBP_DISP32);
        emitU32(addr);
        break;
    case Register::ESP:
        // mov [addr], esp
        code_.push_back(x86::MOV_RM32_TO_R32);
        code_.push_back(x86::ModRM::ESP_DISP32);
        emitU32(addr);
        break;
    }
}

void X86CodeBuilder::movFromMem(Register reg, uint32_t addr)
{
    switch (reg)
    {
    case Register::EAX:
        // mov eax, [addr] (A1 opcode is special form for EAX)
        code_.push_back(x86::MOV_MOFFS32_TO_EAX);
        emitU32(addr);
        code_.push_back(x86::NOP); // Alignment NOP from original code
        break;
    case Register::EBX:
        // mov ebx, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::EBX_DISP32);
        emitU32(addr);
        break;
    case Register::ECX:
        // mov ecx, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::ECX_DISP32);
        emitU32(addr);
        break;
    case Register::EDX:
        // mov edx, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::EDX_DISP32);
        emitU32(addr);
        break;
    case Register::ESI:
        // mov esi, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::ESI_DISP32);
        emitU32(addr);
        break;
    case Register::EDI:
        // mov edi, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::EDI_DISP32);
        emitU32(addr);
        break;
    case Register::EBP:
        // mov ebp, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::EBP_DISP32);
        emitU32(addr);
        break;
    case Register::ESP:
        // mov esp, [addr]
        code_.push_back(x86::MOV_R32_TO_RM32);
        code_.push_back(x86::ModRM::ESP_DISP32);
        emitU32(addr);
        break;
    }
}

void X86CodeBuilder::setByteAtMem(uint32_t addr, uint8_t value)
{
    // mov byte ptr [addr], value
    code_.push_back(x86::MOV_IMM8_TO_RM8);
    code_.push_back(x86::ModRM::MEM_DISP32);
    emitU32(addr);
    code_.push_back(value);
}

void X86CodeBuilder::appendBytes(const std::vector<uint8_t>& bytes)
{
    code_.insert(code_.end(), bytes.begin(), bytes.end());
}

void X86CodeBuilder::jmpRel32(uintptr_t from, uintptr_t dest)
{
    // jmp rel32
    code_.push_back(x86::JMP_REL32);
    uint32_t offset = Rel32From(from, dest);
    emitU32(offset);
}

} // namespace dqxclarity
