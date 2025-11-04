#include "HookBase.hpp"
#include "../pattern/PatternFinder.hpp"
#include "../memory/MemoryPatch.hpp"
#include "../util/Profile.hpp"
#include "Codegen.hpp"

#include <cstring>

namespace dqxclarity
{

HookBase::HookBase(const HookCreateInfo& create_info)
    : memory_(create_info.memory)
    , logger_(create_info.logger)
    , verbose_(create_info.verbose)
    , instruction_safe_steal_(create_info.instruction_safe_steal)
    , readback_bytes_(create_info.readback_bytes)
    , cached_regions_(create_info.cached_regions)
    , on_original_bytes_changed_(create_info.on_original_bytes_changed)
    , on_hook_site_changed_(create_info.on_hook_site_changed)
    , is_installed_(false)
    , hook_address_(0)
    , detour_address_(0)
    , backup_address_(0)
{
}

HookBase::~HookBase()
{
    if (is_installed_)
    {
        RemoveHook();
    }
}

bool HookBase::InstallHook(bool enable_patch)
{
    if (is_installed_ && enable_patch)
    {
        if (logger_.info)
            logger_.info("Hook already installed");
        return true;
    }

    if (verbose_ && logger_.info)
        logger_.info("Installing hook...");

    // Step 1: Find the hook trigger address
    if (!FindTargetAddress())
    {
        if (logger_.error)
            logger_.error("Failed to find hook trigger address");
        return false;
    }

    if (verbose_ && logger_.info)
    {
        logger_.info("Hook trigger found at: 0x" + std::to_string(static_cast<unsigned long long>(hook_address_)));
    }

    // Diagnostic: Read and log bytes at hook location
    if (verbose_ && logger_.debug)
    {
        std::vector<uint8_t> hook_bytes(20);
        if (memory_->ReadMemory(hook_address_, hook_bytes.data(), 20))
        {
            std::string hex;
            for (size_t i = 0; i < 20; ++i)
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", hook_bytes[i]);
                hex += buf;
            }
            logger_.debug("Bytes at hook location: " + hex);
        }
    }

    // Step 2: Allocate memory for detour
    if (!AllocateDetourMemory())
    {
        if (logger_.error)
            logger_.error("Failed to allocate detour memory");
        return false;
    }

    if (verbose_ && logger_.info)
    {
        logger_.info("Detour address: 0x" + std::to_string(static_cast<unsigned long long>(detour_address_)));
        logger_.info("Backup address: 0x" + std::to_string(static_cast<unsigned long long>(backup_address_)));
    }

    // Step 3: Read original bytes FIRST (before writing detour)
    size_t stolen_bytes = instruction_safe_steal_ ? ComputeStolenLength() : 10;
    if (stolen_bytes < 5)
        stolen_bytes = 10; // safety
    original_bytes_.resize(stolen_bytes);
    if (!memory_->ReadMemory(hook_address_, original_bytes_.data(), stolen_bytes))
    {
        if (logger_.error)
            logger_.error("Failed to read original bytes at hook site");
        return false;
    }

    if (verbose_ && logger_.debug)
    {
        std::string hex;
        for (size_t i = 0; i < stolen_bytes; ++i)
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", original_bytes_[i]);
            hex += buf;
        }
        logger_.debug("Original bytes (stolen=" + std::to_string(stolen_bytes) + "): " + hex);
    }

    // Step 4: Write detour code (now that we have stolen bytes)
    if (!WriteDetourCode())
    {
        if (logger_.error)
            logger_.error("Failed to write detour code");
        return false;
    }

    if (!enable_patch)
    {
        // Defer patching until first integrity run
        return true;
    }

    return EnablePatch();
}

bool HookBase::EnablePatch()
{
    if (!RefreshOriginalBytes())
    {
        if (logger_.error)
            logger_.error("Failed to refresh hook bytes before patch");
        return false;
    }

    if (!PatchOriginalFunction())
    {
        if (logger_.error)
            logger_.error("Failed to patch original function");
        return false;
    }

    // Diagnostic: Verify the patch was applied
    if (verbose_ && logger_.debug)
    {
        std::vector<uint8_t> patched_bytes(readback_bytes_);
        if (memory_->ReadMemory(hook_address_, patched_bytes.data(), patched_bytes.size()))
        {
            std::string hex;
            for (size_t i = 0; i < patched_bytes.size(); ++i)
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", patched_bytes[i]);
                hex += buf;
            }
            logger_.debug("Bytes after patching: " + hex);
        }
    }

    is_installed_ = true;
    if (verbose_ && logger_.info)
        logger_.info("Hook installed successfully!");
    return true;
}

bool HookBase::RemoveHook()
{
    if (!is_installed_)
    {
        return true;
    }

    try
    {
        if (verbose_ && logger_.info)
            logger_.info("Removing hook...");

        RestoreOriginalFunction();

        if (detour_address_ != 0)
        {
            memory_->FreeMemory(detour_address_, 4096);
            detour_address_ = 0;
        }

        if (backup_address_ != 0)
        {
            memory_->FreeMemory(backup_address_, 256);
            backup_address_ = 0;
        }

        is_installed_ = false;
        if (verbose_ && logger_.info)
            logger_.info("Hook removed successfully");
        return true;
    }
    catch (const std::exception& e)
    {
        if (logger_.error)
            logger_.error("Exception during hook cleanup: " + std::string(e.what()));
        is_installed_ = false;
        return false;
    }
}

bool HookBase::ReapplyPatch()
{
    if (original_bytes_.empty())
    {
        return false;
    }

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9); // JMP rel32
    const uint32_t jump_offset = Rel32From(hook_address_, detour_address_);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<const uint8_t*>(&jump_offset),
                       reinterpret_cast<const uint8_t*>(&jump_offset) + sizeof(uint32_t));

    // Pad with NOPs to match stolen length
    while (patch_bytes.size() < original_bytes_.size())
    {
        patch_bytes.push_back(0x90); // NOP
    }

    if (!MemoryPatch::WriteWithProtect(*memory_, hook_address_, patch_bytes))
    {
        if (logger_.error)
            logger_.error("Failed to reapply hook patch");
        return false;
    }

    if (verbose_ && logger_.debug)
    {
        logger_.debug("Hook patch reapplied at 0x" + std::to_string(static_cast<unsigned long long>(hook_address_)));
    }

    return true;
}

bool HookBase::IsPatched() const
{
    if (hook_address_ == 0 || detour_address_ == 0 || original_bytes_.empty())
    {
        return false;
    }

    std::vector<uint8_t> current(original_bytes_.size());
    if (!memory_->ReadMemory(hook_address_, current.data(), current.size()))
    {
        return false;
    }

    // Check if first byte is 0xE9 (JMP rel32)
    if (current.size() < 5 || current[0] != 0xE9)
    {
        return false;
    }

    // Verify the JMP target matches our detour
    uint32_t rel_offset = 0;
    std::memcpy(&rel_offset, &current[1], sizeof(uint32_t));
    uintptr_t target = hook_address_ + 5 + static_cast<int32_t>(rel_offset);

    return target == detour_address_;
}

bool HookBase::FindTargetAddress()
{
    PROFILE_SCOPE_FUNCTION();
    auto pattern = GetSignature();

    if (verbose_ && logger_.debug)
    {
        std::string pattern_str;
        for (size_t i = 0; i < pattern.Size(); ++i)
        {
            if (pattern.mask[i])
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", pattern.bytes[i]);
                pattern_str += buf;
            }
            else
            {
                pattern_str += "?? ";
            }
        }
        logger_.debug("Searching for hook trigger using pattern: " + pattern_str);
    }

    PatternFinder finder(memory_);
    bool found = false;

    // Tier 1: Prefer module-restricted scan (use cached regions if available)
    {
        PROFILE_SCOPE_CUSTOM("HookBase.FindInModule");
        uintptr_t addr = 0;

        if (!cached_regions_.empty())
        {
            auto result = finder.FindInModuleWithRegions(pattern, "DQXGame.exe", cached_regions_);
            if (result)
                addr = *result;
        }
        else
        {
            auto result = finder.FindInModule(pattern, "DQXGame.exe");
            if (result)
                addr = *result;
        }

        if (addr != 0)
        {
            hook_address_ = addr;
            found = true;
            if (verbose_ && logger_.info)
                logger_.info("Hook trigger found via FindInModule (Tier 1)");
        }
    }

    // Tier 2: Fallback to executable region scan
    if (!found)
    {
        PROFILE_SCOPE_CUSTOM("HookBase.FindInProcessExec");
        if (auto addr = finder.FindInProcessExec(pattern))
        {
            hook_address_ = *addr;
            found = true;
            if (verbose_ && logger_.info)
                logger_.info("Hook trigger found via FindInProcessExec (Tier 2)");
        }
    }

    // Tier 3: Fallback to naive scan (SLOW)
    if (!found)
    {
        PROFILE_SCOPE_CUSTOM("HookBase.FindWithFallback");
        if (logger_.warn)
            logger_.warn("Hook trigger not found in Tier 1/2, falling back to naive scan (Tier 3)");

        uintptr_t base = memory_->GetModuleBaseAddress("DQXGame.exe");
        if (base == 0)
        {
            if (logger_.error)
                logger_.error("Failed to get DQXGame.exe base address");
            return false;
        }

        if (auto addr = finder.FindWithFallback(pattern, "DQXGame.exe", 80u * 1024u * 1024u))
        {
            hook_address_ = *addr;
            found = true;
            if (verbose_ && logger_.info)
                logger_.info("Hook trigger found via FindWithFallback (Tier 3 - naive scan)");
        }
    }

    return found;
}

bool HookBase::AllocateDetourMemory()
{
    detour_address_ = memory_->AllocateMemory(4096, true); // executable
    if (detour_address_ == 0)
    {
        return false;
    }

    backup_address_ = memory_->AllocateMemory(256, false); // data
    if (backup_address_ == 0)
    {
        memory_->FreeMemory(detour_address_, 4096);
        detour_address_ = 0;
        return false;
    }

    // Initialize flag byte to 0
    uint8_t zero = 0;
    constexpr size_t kFlagOffset = 32;
    memory_->WriteMemory(backup_address_ + kFlagOffset, &zero, sizeof(zero));

    return true;
}

bool HookBase::WriteDetourCode()
{
    auto detour_code = GenerateDetourPayload();
    if (detour_code.empty())
    {
        if (logger_.error)
            logger_.error("Failed to generate detour payload");
        return false;
    }

    if (!memory_->WriteMemory(detour_address_, detour_code.data(), detour_code.size()))
    {
        if (logger_.error)
            logger_.error("Failed to write detour code to allocated memory");
        return false;
    }

    memory_->FlushInstructionCache(detour_address_, detour_code.size());
    return true;
}

bool HookBase::PatchOriginalFunction()
{
    if (original_bytes_.empty())
    {
        return false;
    }

    std::vector<uint8_t> patch_bytes;
    patch_bytes.push_back(0xE9); // JMP rel32
    const uint32_t jump_offset = Rel32From(hook_address_, detour_address_);
    patch_bytes.insert(patch_bytes.end(), reinterpret_cast<const uint8_t*>(&jump_offset),
                       reinterpret_cast<const uint8_t*>(&jump_offset) + sizeof(uint32_t));

    // Pad with NOPs
    while (patch_bytes.size() < original_bytes_.size())
    {
        patch_bytes.push_back(0x90); // NOP
    }

    if (!MemoryPatch::WriteWithProtect(*memory_, hook_address_, patch_bytes))
    {
        return false;
    }

    if (verbose_ && logger_.debug)
    {
        logger_.debug("Hook patched at 0x" + std::to_string(static_cast<unsigned long long>(hook_address_)));
    }

    return true;
}

void HookBase::RestoreOriginalFunction()
{
    if (hook_address_ != 0 && !original_bytes_.empty())
    {
        if (MemoryPatch::WriteWithProtect(*memory_, hook_address_, original_bytes_))
        {
            memory_->FlushInstructionCache(hook_address_, original_bytes_.size());
            if (verbose_ && logger_.debug)
            {
                logger_.debug("Original bytes restored at 0x" +
                              std::to_string(static_cast<unsigned long long>(hook_address_)));
            }
        }
        else
        {
            if (logger_.error)
                logger_.error("Failed to restore original bytes during cleanup");
        }
    }
}

bool HookBase::RefreshOriginalBytes()
{
    if (memory_ == nullptr)
    {
        return false;
    }

    if (IsPatched())
    {
        return true;
    }

    const auto& pattern = GetSignature();

    // Check if current address still matches signature
    auto matches_at = [&](uintptr_t addr) -> bool
    {
        if (addr == 0)
            return false;
        std::vector<uint8_t> buf(pattern.Size());
        if (!memory_->ReadMemory(addr, buf.data(), buf.size()))
            return false;
        for (size_t i = 0; i < pattern.Size(); ++i)
        {
            if (!pattern.mask[i])
                continue;
            if (buf[i] != pattern.bytes[i])
                return false;
        }
        return true;
    };

    uintptr_t located_addr = 0;
    if (matches_at(hook_address_))
    {
        located_addr = hook_address_;
    }
    else
    {
        // Re-scan for signature
        PatternFinder finder(memory_);
        std::optional<uintptr_t> addr;

        if (!cached_regions_.empty())
        {
            addr = finder.FindInModuleWithRegions(pattern, "DQXGame.exe", cached_regions_);
        }
        else
        {
            addr = finder.FindInModule(pattern, "DQXGame.exe");
        }

        if (!addr)
        {
            addr = finder.FindInProcessExec(pattern);
        }

        if (addr)
        {
            located_addr = *addr;
        }
    }

    if (located_addr == 0)
    {
        if (logger_.error)
            logger_.error("Hook signature missing when refreshing");
        return false;
    }

    const bool address_changed = (located_addr != hook_address_);
    uintptr_t previous_addr = hook_address_;
    hook_address_ = located_addr;

    size_t new_len = instruction_safe_steal_ ? ComputeStolenLength() : 10;
    if (new_len < 5)
        new_len = 10;

    std::vector<uint8_t> latest(new_len);
    if (!memory_->ReadMemory(hook_address_, latest.data(), latest.size()))
    {
        return false;
    }

    bool bytes_changed = original_bytes_.size() != latest.size();
    if (!bytes_changed && !original_bytes_.empty())
    {
        bytes_changed = !std::equal(latest.begin(), latest.end(), original_bytes_.begin());
    }

    if (!address_changed && !bytes_changed)
    {
        return true;
    }

    if (bytes_changed && !original_bytes_.empty())
    {
        if (logger_.warn)
        {
            size_t mismatch = 0;
            size_t limit = original_bytes_.size();
            if (latest.size() < limit)
                limit = latest.size();
            while (mismatch < limit && original_bytes_[mismatch] == latest[mismatch])
            {
                ++mismatch;
            }
            logger_.warn("Hook prologue changed; mismatch index=" + std::to_string(mismatch));
        }
    }

    original_bytes_ = std::move(latest);

    if (!WriteDetourCode())
    {
        return false;
    }

    if (on_original_bytes_changed_)
    {
        on_original_bytes_changed_(hook_address_, original_bytes_);
    }
    if (address_changed && on_hook_site_changed_)
    {
        on_hook_site_changed_(previous_addr, hook_address_, original_bytes_);
    }

    return true;
}

size_t HookBase::ComputeStolenLength()
{
    // Use simple instruction length decoder
    // This is a simplified version - full implementation would use ldisasm or similar
    size_t offset = 0;
    size_t count = 0;
    constexpr size_t kMaxInstructions = 10;
    constexpr size_t kMinStolen = 5;

    std::vector<uint8_t> code(32);
    if (!memory_->ReadMemory(hook_address_, code.data(), code.size()))
    {
        return 10; // fallback
    }

    while (offset < kMinStolen && count < kMaxInstructions && offset < code.size())
    {
        uint8_t opcode = code[offset];

        // Simple x86 instruction length heuristics (very basic)
        size_t len = 1;
        if (opcode == 0x89 || opcode == 0x8B || opcode == 0x8D)
        { // MOV/LEA with ModRM
            len = 2;
            if (offset + 1 < code.size())
            {
                uint8_t modrm = code[offset + 1];
                uint8_t mod = (modrm >> 6) & 0x3;
                uint8_t rm = modrm & 0x7;
                if (mod == 0 && rm == 5)
                    len += 4; // disp32
                else if (mod == 1)
                    len += 1; // disp8
                else if (mod == 2)
                    len += 4; // disp32
                if (mod != 3 && rm == 4)
                    len += 1; // SIB
            }
        }
        else if (opcode == 0xE8 || opcode == 0xE9)
        { // CALL/JMP rel32
            len = 5;
        }
        else if (opcode == 0x6A)
        { // PUSH imm8
            len = 2;
        }
        else if (opcode == 0x68)
        { // PUSH imm32
            len = 5;
        }
        else if (opcode >= 0x50 && opcode <= 0x5F)
        { // PUSH/POP reg
            len = 1;
        }

        offset += len;
        count++;
    }

    return (offset >= kMinStolen) ? offset : 10;
}

std::vector<uint8_t> HookBase::BuildStandardDetour(const std::vector<uint8_t>& register_backup_code,
                                                     const std::vector<uint8_t>& capture_code,
                                                     const std::vector<uint8_t>& register_restore_code)
{
    std::vector<uint8_t> detour;

    // 1. Register backup
    detour.insert(detour.end(), register_backup_code.begin(), register_backup_code.end());

    // 2. Hook-specific capture logic
    detour.insert(detour.end(), capture_code.begin(), capture_code.end());

    // 3. Register restore
    detour.insert(detour.end(), register_restore_code.begin(), register_restore_code.end());

    // 4. Stolen instructions
    detour.insert(detour.end(), original_bytes_.begin(), original_bytes_.end());

    // 5. Jump back to original code
    detour.push_back(0xE9); // JMP rel32
    uintptr_t return_address = hook_address_ + original_bytes_.size();
    uintptr_t jmp_from = detour_address_ + detour.size();
    uint32_t rel_offset = Rel32From(jmp_from, return_address);
    const uint8_t* rel_ptr = reinterpret_cast<const uint8_t*>(&rel_offset);
    detour.insert(detour.end(), rel_ptr, rel_ptr + sizeof(uint32_t));

    return detour;
}

} // namespace dqxclarity

