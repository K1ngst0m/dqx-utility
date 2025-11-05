#include "HookManager.hpp"

#include "DialogHook.hpp"
#include "QuestHook.hpp"
#include "PlayerHook.hpp"
#include "CornerTextHook.hpp"
#include "NetworkTextHook.hpp"
#include "IntegrityHook.hpp"
#include "IntegrityMonitor.hpp"
#include "IHook.hpp"

#include <chrono>

namespace dqxclarity
{

bool HookManager::RegisterHook(
    persistence::HookType type,
    const HookCreateInfo& info,
    IntegrityHook* integrity,
    IntegrityMonitor* monitor)
{
    // Store memory and logger for later use
    if (!memory_)
    {
        memory_ = info.memory;
        logger_ = info.logger;
    }

    // Create HookCreateInfo with integrity callbacks
    HookCreateInfo hook_info = info;
    
    // Wire integrity callbacks (Dialog hook needs these for RefreshOriginalBytes)
    // Other hooks may need them in the future, so we wire them for all hooks
    hook_info.on_original_bytes_changed = [integrity, monitor](uintptr_t addr, const std::vector<uint8_t>& bytes)
    {
        if (integrity)
        {
            integrity->UpdateRestoreTarget(addr, bytes);
        }
        if (monitor)
        {
            monitor->UpdateRestoreTarget(addr, bytes);
        }
    };
    
    hook_info.on_hook_site_changed = [integrity, monitor](uintptr_t old_addr, uintptr_t new_addr, const std::vector<uint8_t>& bytes)
    {
        if (integrity)
        {
            integrity->MoveRestoreTarget(old_addr, new_addr, bytes);
        }
        if (monitor)
        {
            monitor->MoveRestoreTarget(old_addr, new_addr, bytes);
        }
    };

    // Create the appropriate hook type
    std::unique_ptr<IHook> hook;
    
    switch (type)
    {
        case persistence::HookType::Dialog:
            hook = std::make_unique<DialogHook>(hook_info);
            break;
            
        case persistence::HookType::Quest:
            hook = std::make_unique<QuestHook>(hook_info);
            break;
            
        case persistence::HookType::Player:
            hook = std::make_unique<PlayerHook>(hook_info);
            break;
            
        case persistence::HookType::Corner:
            hook = std::make_unique<CornerTextHook>(hook_info);
            break;
            
        case persistence::HookType::Network:
            hook = std::make_unique<NetworkTextHook>(hook_info);
            break;
            
        case persistence::HookType::Integrity:
            hook = std::make_unique<IntegrityHook>(hook_info);
            break;
            
        default:
            if (logger_.error)
                logger_.error("HookManager::RegisterHook called with unknown hook type");
            return false;
    }

    // Install hook (deferred patch - enable later based on policy)
    if (!hook->InstallHook(/*enable_patch=*/false))
    {
        if (logger_.warn)
        {
            logger_.warn("Failed to install " + GetHookTypeName(type) + " hook");
        }
        return false;
    }

    // Register with HookRegistry for crash recovery persistence
    if (hook->GetHookAddress() != 0)
    {
        try
        {
            persistence::HookRecord record;
            record.type = type;
            record.process_id = memory_->GetAttachedPid();
            record.hook_address = hook->GetHookAddress();
            record.detour_address = hook->GetDetourAddress();
            record.detour_size = 4096;
            record.backup_address = hook->GetBackupAddress();
            record.backup_size = 256;
            record.original_bytes = hook->GetOriginalBytes();
            record.installed_time = std::chrono::system_clock::now();
            record.hook_checksum = persistence::HookRegistry::ComputeCRC32(
                record.original_bytes.data(), record.original_bytes.size());
            record.detour_checksum = 0;
            
            persistence::HookRegistry::RegisterHook(record);
        }
        catch (const std::exception& e)
        {
            if (logger_.warn)
            {
                logger_.warn("Failed to register " + GetHookTypeName(type) + " hook in persistence: " + e.what());
            }
        }
    }

    // Store hook instance
    hooks_[type] = std::move(hook);
    
    if (logger_.info)
    {
        logger_.info(GetHookTypeName(type) + " hook installed successfully");
    }
    
    return true;
}

void HookManager::RemoveAllHooks()
{
    // Remove hooks and unregister from persistence
    for (auto& [type, hook] : hooks_)
    {
        if (hook)
        {
            // Remove the hook (restore original bytes)
            hook->RemoveHook();
            
            // Unregister from persistence
            try
            {
                persistence::HookRegistry::UnregisterHook(type);
            }
            catch (const std::exception& e)
            {
                if (logger_.warn)
                {
                    logger_.warn("Failed to unregister " + GetHookTypeName(type) + " hook from persistence: " + e.what());
                }
            }
        }
    }
    
    // Clear all hooks
    hooks_.clear();
    
    if (logger_.info)
        logger_.info("All hooks removed");
}

IHook* HookManager::GetHook(persistence::HookType type)
{
    auto it = hooks_.find(type);
    if (it != hooks_.end())
    {
        return it->second.get();
    }
    return nullptr;
}

IntegrityHook* HookManager::GetIntegrityHook()
{
    auto* hook = GetHook(persistence::HookType::Integrity);
    return dynamic_cast<IntegrityHook*>(hook);
}

void HookManager::WireIntegrityCallbacks(IntegrityHook* integrity, IntegrityMonitor* monitor)
{
    // Add all registered hooks (except integrity itself) as restore targets
    // This ensures integrity can restore original bytes when the game's anti-cheat runs
    
    for (const auto& [type, hook] : hooks_)
    {
        if (!hook || hook->GetHookAddress() == 0)
            continue;
        
        // Skip the integrity hook itself - don't add it as a restore target
        if (type == persistence::HookType::Integrity)
            continue;
            
        // Add to integrity hook
        if (integrity)
        {
            integrity->AddRestoreTarget(hook->GetHookAddress(), hook->GetOriginalBytes());
        }
        
        // Add to integrity monitor
        if (monitor)
        {
            monitor->AddRestoreTarget(hook->GetHookAddress(), hook->GetOriginalBytes());
        }
    }
    
    if (logger_.info && (integrity || monitor))
    {
        size_t count = hooks_.size() - (hooks_.count(persistence::HookType::Integrity) > 0 ? 1 : 0);
        logger_.info("Wired integrity callbacks for " + std::to_string(count) + " hooks");
    }
}

void HookManager::EnableAllPatches(const Logger& logger)
{
    for (const auto& [type, hook] : hooks_)
    {
        if (hook)
        {
            hook->EnablePatch();
            if (logger.info)
            {
                logger.info(GetHookTypeName(type) + " hook enabled");
            }
        }
    }
}

void HookManager::ReapplyAllPatches(const Logger& logger)
{
    for (const auto& [type, hook] : hooks_)
    {
        if (hook)
        {
            hook->ReapplyPatch();
            if (logger.info)
            {
                logger.info(GetHookTypeName(type) + " hook re-applied");
            }
        }
    }
}

void HookManager::VerifyAllPatches(const Logger& logger, bool verbose)
{
    for (const auto& [type, hook] : hooks_)
    {
        if (!hook)
            continue;
            
        if (!hook->IsPatched())
        {
            if (logger.warn)
            {
                logger.warn(GetHookTypeName(type) + " hook not present; reapplying");
            }
            hook->ReapplyPatch();
        }
        else if (verbose && logger.info)
        {
            logger.info(GetHookTypeName(type) + " hook verified present");
        }
    }
}

std::string HookManager::GetHookTypeName(persistence::HookType type)
{
    switch (type)
    {
        case persistence::HookType::Dialog: return "Dialog";
        case persistence::HookType::Quest: return "Quest";
        case persistence::HookType::Player: return "Player";
        case persistence::HookType::Corner: return "Corner";
        case persistence::HookType::Network: return "Network";
        case persistence::HookType::Integrity: return "Integrity";
        default: return "Unknown";
    }
}

} // namespace dqxclarity

