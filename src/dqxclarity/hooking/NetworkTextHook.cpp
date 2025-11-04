#include "NetworkTextHook.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"

namespace dqxclarity
{

NetworkTextHook::NetworkTextHook(const HookCreateInfo& create_info)
    : HookBase(create_info)
{
}

Pattern NetworkTextHook::GetSignature() const
{
    return Signatures::GetNetworkText();
}

std::vector<uint8_t> NetworkTextHook::GenerateDetourPayload()
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

size_t NetworkTextHook::ComputeStolenLength()
{
    return kDefaultStolenBytes;
}

bool NetworkTextHook::PollNetworkText()
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

    uint8_t zero = 0;
    memory()->WriteMemory(backup_address() + kFlagOffset, &zero, sizeof(zero));

    uint32_t text_ptr_raw = 0;
    uint32_t category_ptr_raw = 0;
    if (!memory()->ReadMemory(backup_address() + kTextRegisterOffset, &text_ptr_raw, sizeof(text_ptr_raw)))
    {
        return false;
    }
    if (!memory()->ReadMemory(backup_address() + kCategoryRegisterOffset, &category_ptr_raw, sizeof(category_ptr_raw)))
    {
        return false;
    }

    const uintptr_t text_ptr = static_cast<uintptr_t>(text_ptr_raw);
    const uintptr_t category_ptr = static_cast<uintptr_t>(category_ptr_raw);

    last_capture_.text_ptr = text_ptr;
    last_capture_.category_ptr = category_ptr;

    std::string category;
    std::string text;
    if (category_ptr != 0)
    {
        if (!memory()->ReadString(category_ptr, category, kMaxCategoryLength))
        {
            category.clear();
        }
    }
    if (text_ptr != 0)
    {
        if (!memory()->ReadString(text_ptr, text, kMaxTextLength))
        {
            text.clear();
        }
    }

    last_capture_.category = std::move(category);
    last_capture_.text = std::move(text);
    return true;
}

} // namespace dqxclarity
