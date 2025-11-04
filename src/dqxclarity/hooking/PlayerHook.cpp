#include "PlayerHook.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"

namespace dqxclarity
{

PlayerHook::PlayerHook(const HookCreateInfo& create_info)
    : HookBase(create_info)
{
}

Pattern PlayerHook::GetSignature() const
{
    return Signatures::GetPlayerNameTrigger();
}

std::vector<uint8_t> PlayerHook::GenerateDetourPayload()
{
    std::vector<uint8_t> code;

    // 1. Backup all registers
    X86CodeBuilder backup_builder;
    backup_builder.movToMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(backup_address()));
    backup_builder.movToMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(backup_address() + 4));
    backup_builder.movToMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(backup_address() + 8));
    backup_builder.movToMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(backup_address() + 12));
    backup_builder.movToMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(backup_address() + 16));
    backup_builder.movToMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(backup_address() + 20));
    backup_builder.movToMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(backup_address() + 24));
    backup_builder.movToMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(backup_address() + 28));
    const auto& backup_code = backup_builder.code();
    code.insert(code.end(), backup_code.begin(), backup_code.end());

    // 2. Set new data flag
    X86CodeBuilder flag_builder;
    flag_builder.setByteAtMem(static_cast<uint32_t>(backup_address() + kFlagOffset), 0x01);
    const auto& flag_code = flag_builder.code();
    code.insert(code.end(), flag_code.begin(), flag_code.end());

    // 3. Restore all registers
    X86CodeBuilder restore_builder;
    restore_builder.movFromMem(X86CodeBuilder::Register::EAX, static_cast<uint32_t>(backup_address()));
    restore_builder.movFromMem(X86CodeBuilder::Register::EBX, static_cast<uint32_t>(backup_address() + 4));
    restore_builder.movFromMem(X86CodeBuilder::Register::ECX, static_cast<uint32_t>(backup_address() + 8));
    restore_builder.movFromMem(X86CodeBuilder::Register::EDX, static_cast<uint32_t>(backup_address() + 12));
    restore_builder.movFromMem(X86CodeBuilder::Register::ESI, static_cast<uint32_t>(backup_address() + 16));
    restore_builder.movFromMem(X86CodeBuilder::Register::EDI, static_cast<uint32_t>(backup_address() + 20));
    restore_builder.movFromMem(X86CodeBuilder::Register::EBP, static_cast<uint32_t>(backup_address() + 24));
    restore_builder.movFromMem(X86CodeBuilder::Register::ESP, static_cast<uint32_t>(backup_address() + 28));
    const auto& restore_code = restore_builder.code();
    code.insert(code.end(), restore_code.begin(), restore_code.end());

    // 4. Append stolen instructions
    code.insert(code.end(), stolen_bytes().begin(), stolen_bytes().end());

    // 5. Jump back to original code
    X86CodeBuilder jmp_builder;
    uintptr_t return_addr = hook_address() + stolen_bytes().size();
    uintptr_t jmp_from = detour_address() + code.size();
    jmp_builder.jmpRel32(jmp_from, return_addr);
    const auto& jmp_code = jmp_builder.code();
    code.insert(code.end(), jmp_code.begin(), jmp_code.end());

    return code;
}

size_t PlayerHook::ComputeStolenLength()
{
    return kDefaultStolenBytes;
}

bool PlayerHook::PollPlayerData()
{
    if (!IsHookInstalled() || backup_address() == 0)
    {
        return false;
    }

    uint8_t flag = 0;
    if (!memory()->ReadMemory(backup_address() + kFlagOffset, &flag, sizeof(flag)))
    {
        return false;
    }

    if (flag == 0)
    {
        return false;
    }

    uint32_t ptr_raw = 0;
    if (!memory()->ReadMemory(backup_address(), &ptr_raw, sizeof(ptr_raw)))
    {
        uint8_t zero = 0;
        memory()->WriteMemory(backup_address() + kFlagOffset, &zero, sizeof(zero));
        return false;
    }

    uint8_t zero = 0;
    memory()->WriteMemory(backup_address() + kFlagOffset, &zero, sizeof(zero));

    if (ptr_raw == 0)
    {
        return false;
    }

    const uintptr_t struct_ptr = static_cast<uintptr_t>(ptr_raw);

    PlayerInfo data;
    auto read_field = [&](uint32_t offset, std::string& out)
    {
        std::string value;
        if (memory()->ReadString(struct_ptr + offset, value, kMaxStringLength))
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
    if (memory()->ReadMemory(struct_ptr + kRelationshipOffset, &rel_byte, sizeof(rel_byte)))
    {
        data.relationship = DecodeRelationship(rel_byte);
    }
    else
    {
        data.relationship = PlayerRelationship::Unknown;
    }

    last_data_ = std::move(data);
    return true;
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

} // namespace dqxclarity
