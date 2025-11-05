#pragma once

#include "HookCreateInfo.hpp"
#include "HookRegistry.hpp"
#include "../api/dqxclarity.hpp"
#include "../memory/IProcessMemory.hpp"

#include <map>
#include <memory>

namespace dqxclarity
{

class IntegrityHook;
class IntegrityMonitor;
class IHook;

/**
 * @brief Centralized lifecycle manager for all game hooks
 * 
 * Manages hook creation, persistence, and integrity system integration.
 * Provides batch operations for enabling/disabling hooks based on policy.
 */
class HookManager
{
public:
    HookManager() = default;
    ~HookManager() = default;

    // Non-copyable, non-movable (owns unique hook instances)
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

    /**
     * @brief Register a hook with automatic persistence and integrity wiring
     * 
     * Creates the appropriate hook type, installs it (deferred patch),
     * registers with HookRegistry for crash recovery, and wires integrity callbacks.
     * 
     * @param type Hook type to create (Dialog, Quest, Player, Corner, Network, Integrity)
     * @param info Hook configuration (memory, logger, settings)
     * @param integrity Integrity hook instance for callback wiring (can be nullptr)
     * @param monitor Integrity monitor instance for callback wiring (can be nullptr)
     * @return true if hook was successfully created and installed
     */
    bool RegisterHook(
        persistence::HookType type,
        const HookCreateInfo& info,
        IntegrityHook* integrity,
        IntegrityMonitor* monitor);

    /**
     * @brief Remove all hooks and unregister from persistence
     * 
     * Calls RemoveHook() on each hook, then unregisters from HookRegistry.
     * Safe to call multiple times.
     */
    void RemoveAllHooks();

    /**
     * @brief Access a hook by type
     * 
     * @param type Hook type to retrieve
     * @return Pointer to hook instance, or nullptr if not registered
     */
    IHook* GetHook(persistence::HookType type);
    
    /**
     * @brief Access the integrity hook with proper type
     * 
     * @return Pointer to IntegrityHook instance, or nullptr if not registered
     */
    IntegrityHook* GetIntegrityHook();

    /**
     * @brief Wire integrity callbacks to all registered hooks
     * 
     * Should be called after IntegrityHook and IntegrityMonitor are created.
     * Updates hook callbacks so they notify integrity system when hooks are refreshed.
     * 
     * @param integrity Integrity hook instance (can be nullptr)
     * @param monitor Integrity monitor instance (can be nullptr)
     */
    void WireIntegrityCallbacks(IntegrityHook* integrity, IntegrityMonitor* monitor);

    /**
     * @brief Enable patches on all registered hooks
     * 
     * @param logger Logger for diagnostics
     */
    void EnableAllPatches(const Logger& logger);

    /**
     * @brief Reapply patches on all registered hooks
     * 
     * @param logger Logger for diagnostics
     */
    void ReapplyAllPatches(const Logger& logger);

    /**
     * @brief Verify all hooks are patched and reapply if not
     * 
     * @param logger Logger for diagnostics
     * @param verbose Enable verbose logging
     */
    void VerifyAllPatches(const Logger& logger, bool verbose);

private:
    static std::string GetHookTypeName(persistence::HookType type);
    // Hook instances keyed by type
    std::map<persistence::HookType, std::unique_ptr<IHook>> hooks_;

    // Process memory interface
    IProcessMemory* memory_ = nullptr;

    // Logger for hook manager diagnostics
    Logger logger_;
};

} // namespace dqxclarity

