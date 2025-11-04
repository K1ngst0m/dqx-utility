#pragma once

#include "IHook.hpp"
#include "HookCreateInfo.hpp"
#include "../pattern/Pattern.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../api/dqxclarity.hpp"

#include <memory>
#include <vector>
#include <cstdint>
#include <functional>

namespace dqxclarity
{

/**
 * @brief Base class implementing common hook infrastructure
 * 
 * Derived classes implement GetSignature() and GenerateDetourPayload().
 * All other hooking logic (pattern scanning, memory allocation, patching) is handled here.
 */
class HookBase : public IHook
{
public:
    explicit HookBase(const HookCreateInfo& create_info);
    virtual ~HookBase();

    // IHook interface implementation
    bool InstallHook(bool enable_patch = true) override;
    bool EnablePatch() override;
    bool RemoveHook() override;
    bool ReapplyPatch() override;
    bool IsPatched() const override;

    uintptr_t GetHookAddress() const override { return hook_address_; }
    uintptr_t GetDetourAddress() const override { return detour_address_; }
    uintptr_t GetBackupAddress() const override { return backup_address_; }
    const std::vector<uint8_t>& GetOriginalBytes() const override { return original_bytes_; }

protected:
    // Pure virtual methods - derived classes must implement
    
    /**
     * @brief Get the signature pattern for finding the hook site
     * @return Pattern to search for in target process
     */
    virtual Pattern GetSignature() const = 0;

    // Pure virtual: hook-specific detour bytecode generation
    virtual std::vector<uint8_t> GenerateDetourPayload() = 0;

    // Protected accessors for payload generation and polling
    uintptr_t hook_address() const { return hook_address_; }
    uintptr_t detour_address() const { return detour_address_; }
    uintptr_t backup_address() const { return backup_address_; }
    const std::vector<uint8_t>& stolen_bytes() const { return original_bytes_; }
    const dqxclarity::Logger& logger() const { return logger_; }
    bool verbose() const { return verbose_; }
    std::shared_ptr<IProcessMemory> memory() const { return memory_; }
    bool IsHookInstalled() const { return is_installed_; }

    // Virtual: override for hook-specific stolen byte computation
    virtual size_t ComputeStolenLength();

    // Helper for standard detour pattern (backup → capture → restore → stolen → jump back)
    std::vector<uint8_t> BuildStandardDetour(
        const std::vector<uint8_t>& register_backup_code,
        const std::vector<uint8_t>& capture_code,
        const std::vector<uint8_t>& register_restore_code);

private:
    // Common infrastructure methods
    bool FindTargetAddress();
    bool AllocateDetourMemory();
    bool WriteDetourCode();
    bool PatchOriginalFunction();
    void RestoreOriginalFunction();
    bool RefreshOriginalBytes();

    // Configuration (immutable after construction)
    std::shared_ptr<IProcessMemory> memory_;
    dqxclarity::Logger logger_;
    bool verbose_;
    bool instruction_safe_steal_;
    size_t readback_bytes_;
    std::vector<MemoryRegion> cached_regions_;

    // Dialog-specific callbacks (optional)
    std::function<void(uintptr_t, const std::vector<uint8_t>&)> on_original_bytes_changed_;
    std::function<void(uintptr_t, uintptr_t, const std::vector<uint8_t>&)> on_hook_site_changed_;

    // Hook state
    bool is_installed_;
    uintptr_t hook_address_;
    uintptr_t detour_address_;
    uintptr_t backup_address_;
    std::vector<uint8_t> original_bytes_;
};

} // namespace dqxclarity

