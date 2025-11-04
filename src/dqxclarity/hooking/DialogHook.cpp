#include "DialogHook.hpp"
#include "../signatures/Signatures.hpp"
#include "Codegen.hpp"

namespace dqxclarity
{

DialogHook::DialogHook(const HookCreateInfo& create_info)
    : HookBase(create_info)
{
}

Pattern DialogHook::GetSignature() const
{
    return Signatures::GetDialogTrigger();
}

std::vector<uint8_t> DialogHook::GenerateDetourPayload()
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

size_t DialogHook::ComputeStolenLength()
{
    // Try to read a small window and recognize the expected prologue
    std::vector<uint8_t> head(16);
    if (!memory()->ReadMemory(hook_address(), head.data(), head.size()))
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
    if (logger().warn)
        logger().warn("Instruction-safe steal: unknown prologue; using 10 bytes fallback");
    return 10;
}

bool DialogHook::PollDialogData()
{
    if (backup_address() == 0)
    {
        return false;
    }

    try
    {
        // Check if new dialog data flag is set (backup+32)
        uint8_t flag = 0;
        if (!memory()->ReadMemory(backup_address() + kFlagOffset, &flag, 1))
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

        if (!memory()->ReadMemory(backup_address() + 16, &text_ptr, 4))
        {
            return false;
        }

        if (!memory()->ReadMemory(backup_address() + 28, &npc_ptr, 4))
        {
            return false;
        }

        // Clear the flag
        uint8_t zero = 0;
        memory()->WriteMemory(backup_address() + kFlagOffset, &zero, 1);

        // Extract text pointer and npc pointer:
        // text_ptr (ESI) is already the address of the text string
        uintptr_t text_address = text_ptr;

        // npc_ptr (ESP): read a 32-bit value at (ESP + 0x14)
        uintptr_t npc_address = memory()->ReadInt32(npc_ptr + 0x14);

        // Read dialog text
        std::string dialog_text;
        if (text_address != 0)
        {
            memory()->ReadString(text_address, dialog_text, kMaxStringLength);
        }

        // Read NPC name
        std::string npc_name = "No_NPC";
        if (npc_address != 0)
        {
            if (!memory()->ReadString(npc_address, npc_name, kMaxStringLength) || npc_name.empty())
            {
                npc_name = "No_NPC";
            }
        }

        last_dialog_text_ = dialog_text;
        last_npc_name_ = npc_name;

        if (console_output_ && console_ && !dialog_text.empty())
        {
            console_->PrintDialog(npc_name, dialog_text);
        }

        return true;
    }
    catch (...)
    {
        // Silently ignore errors to avoid crashes
        return false;
    }
}

} // namespace dqxclarity
