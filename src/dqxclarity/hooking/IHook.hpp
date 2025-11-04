#pragma once

#include <cstdint>
#include <vector>

namespace dqxclarity
{

/**
 * @brief Pure virtual interface for all hook types
 * 
 * This interface defines the common lifecycle and metadata access for hooks.
 * Hook-specific polling methods (PollDialogData, PollQuestData, etc.) are
 * implemented in concrete classes as they have different return types/signatures.
 * 
 * Lifecycle States:
 * 1. Constructed - Hook created, not installed
 * 2. Installed - Detour allocated, original bytes saved, NOT patched
 * 3. Patched - JMP written, hook active
 */
class IHook
{
public:
    virtual ~IHook() = default;

    /**
     * @brief Install hook infrastructure (allocate detour, save original bytes)
     * @param enable_patch If true, apply JMP patch immediately; if false, defer until EnablePatch()
     * @return true on success
     * 
     * This supports deferred patching for integrity system:
     * - Call InstallHook(false) to prepare hook without patching
     * - Later call EnablePatch() after integrity check completes
     */
    virtual bool InstallHook(bool enable_patch = true) = 0;

    /**
     * @brief Apply the JMP patch to activate the hook
     * @return true on success
     */
    virtual bool EnablePatch() = 0;

    /**
     * @brief Remove hook and restore original bytes
     * @return true on success
     */
    virtual bool RemoveHook() = 0;

    /**
     * @brief Reapply the JMP patch (used after integrity restoration)
     * @return true on success
     */
    virtual bool ReapplyPatch() = 0;

    /**
     * @brief Check if hook is currently patched (JMP is active)
     * @return true if patched
     */
    virtual bool IsPatched() const = 0;

    // Metadata access (for registry & integrity)
    virtual uintptr_t GetHookAddress() const = 0;
    virtual uintptr_t GetDetourAddress() const = 0;
    virtual uintptr_t GetBackupAddress() const = 0;
    virtual const std::vector<uint8_t>& GetOriginalBytes() const = 0;
};

} // namespace dqxclarity

