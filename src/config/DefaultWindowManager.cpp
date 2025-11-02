#include "DefaultWindowManager.hpp"

#include "../ui/common/BaseWindowState.hpp"
#include "../ui/WindowRegistry.hpp"
#include "../ui/dialog/DialogStateManager.hpp"
#include "../ui/quest/QuestStateManager.hpp"
#include "../ui/quest/QuestHelperStateManager.hpp"

#include <plog/Log.h>

DefaultWindowManager::DefaultWindowManager(
    std::unique_ptr<IWindowStateOperations> operations,
    const char* section_name)
    : operations_(std::move(operations))
    , section_name_(section_name)
{
}

void DefaultWindowManager::setEnabled(bool enabled, bool suppress_updates, WindowRegistry* registry)
{
    if (enabled_ == enabled)
        return;

    // If disabling, capture current state before it's removed
    if (!enabled && registry)
    {
        captureCurrentState(registry);
    }

    enabled_ = enabled;

    if (!suppress_updates && registry)
    {
        enforceState(registry);
    }
}

void DefaultWindowManager::enforceState(WindowRegistry* registry)
{
    if (!registry)
        return;

    BaseWindowState* current = operations_->getDefaultWindowState();

    if (enabled_)
    {
        // Should have a default window
        if (!current)
        {
            // Create it
            BaseWindowState* new_state = operations_->createDefaultWindow(name_, state_.get());
            if (!name_.empty() && new_state)
            {
                // Name was applied during creation
            }
            else if (new_state)
            {
                // Find the actual default window by comparing state pointers
                auto windows = operations_->getAllWindows();
                auto* default_state = operations_->getDefaultWindowState();
                for (auto* window : windows)
                {
                    if (operations_->getWindowState(window) == default_state)
                    {
                        name_ = operations_->getWindowName(window);
                        break;
                    }
                }
            }
        }
        else
        {
            // Already exists, update our cached name by finding the default window
            auto windows = operations_->getAllWindows();
            for (auto* window : windows)
            {
                if (operations_->getWindowState(window) == current)
                {
                    name_ = operations_->getWindowName(window);
                    break;
                }
            }
        }

        // Update our cached state
        captureCurrentState(registry);
    }
    else
    {
        // Should not have a default window
        if (current)
        {
            captureCurrentState(registry);
            operations_->removeDefaultWindow();
        }
    }
}

void DefaultWindowManager::captureCurrentState(WindowRegistry* registry)
{
    if (!registry)
        return;

    BaseWindowState* current = operations_->getDefaultWindowState();
    if (!current)
        return;

    // Find the default window and get its name
    auto windows = operations_->getAllWindows();
    for (auto* window : windows)
    {
        if (operations_->getWindowState(window) == current)
        {
            name_ = operations_->getWindowName(window);
            break;
        }
    }

    // Clone the state based on type
    // We need to create the appropriate derived type
    if (section_name_ == "dialogs")
    {
        auto* dialog_state = static_cast<DialogStateManager*>(current);
        state_ = std::make_unique<DialogStateManager>(*dialog_state);
    }
    else if (section_name_ == "quests")
    {
        auto* quest_state = static_cast<QuestStateManager*>(current);
        state_ = std::make_unique<QuestStateManager>(*quest_state);
    }
    else if (section_name_ == "quest_helpers")
    {
        auto* quest_helper_state = static_cast<QuestHelperStateManager*>(current);
        state_ = std::make_unique<QuestHelperStateManager>(*quest_helper_state);
    }
}

void DefaultWindowManager::saveState(toml::table& root, WindowRegistry* registry)
{
    if (!registry)
        return;

    // Update state before saving
    if (enabled_)
    {
        captureCurrentState(registry);
    }

    // If we have a state to save (either current or cached), save it
    if (state_ && !name_.empty())
    {
        toml::array arr;
        
        // If enabled, save all windows; otherwise just save the cached state
        if (enabled_)
        {
            auto windows = operations_->getAllWindows();
            for (auto* window : windows)
            {
                // This is a simplified version - full implementation in Phase 2
                std::string window_name = operations_->getWindowName(window);
                // Serialize each window - placeholder for now
            }
        }
        else
        {
            // Save just the cached default state
            arr.push_back(operations_->serializeState(name_, *state_));
        }
        
        if (!arr.empty())
        {
            root.insert(section_name_, std::move(arr));
        }
    }
}

void DefaultWindowManager::loadState(const toml::table& root, bool suppress_updates, WindowRegistry* registry)
{
    (void)suppress_updates; // Unused parameter
    if (!registry)
        return;

    // This is a placeholder - full implementation will come in Phase 2
    // For now, just mark that we need to load from TOML
    
    if (auto* arr = root[section_name_].as_array())
    {
        if (!arr->empty())
        {
            // Load will be properly implemented in Phase 2 with StateSerializer
            PLOG_DEBUG << "DefaultWindowManager: loadState for " << section_name_ << " not yet fully implemented";
        }
    }
}
