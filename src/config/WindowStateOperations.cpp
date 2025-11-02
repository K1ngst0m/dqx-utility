#include "WindowStateOperations.hpp"

#include "StateSerializer.hpp"
#include "WindowStateApplier.hpp"
#include "../ui/WindowRegistry.hpp"
#include "../ui/dialog/DialogWindow.hpp"
#include "../ui/quest/QuestWindow.hpp"
#include "../ui/quest/QuestHelperWindow.hpp"
#include "../ui/dialog/DialogStateManager.hpp"
#include "../ui/quest/QuestStateManager.hpp"
#include "../ui/quest/QuestHelperStateManager.hpp"
#include "../ui/common/BaseWindowState.hpp"

// DialogWindow specialization
template <>
BaseWindowState* WindowStateOperations<DialogWindow, DialogStateManager>::getDefaultWindowState()
{
    if (auto* window = registry_->defaultDialogWindow())
        return &window->state();
    return nullptr;
}

template <>
BaseWindowState* WindowStateOperations<DialogWindow, DialogStateManager>::createDefaultWindow(
    const std::string& name, const BaseWindowState* initial_state)
{
    auto& window = registry_->createDialogWindow(true);
    if (!name.empty())
        window.rename(name.c_str());
    if (initial_state)
    {
        WindowStateApplier::apply(window, static_cast<const DialogStateManager&>(*initial_state));
    }
    return &window.state();
}

template <>
void WindowStateOperations<DialogWindow, DialogStateManager>::removeDefaultWindow()
{
    if (auto* window = registry_->defaultDialogWindow())
        registry_->removeWindow(window);
}

template <>
std::vector<UIWindow*> WindowStateOperations<DialogWindow, DialogStateManager>::getAllWindows()
{
    return registry_->windowsByType(UIWindowType::Dialog);
}

template <>
void WindowStateOperations<DialogWindow, DialogStateManager>::applyStateToWindow(
    UIWindow* window, const BaseWindowState& state)
{
    auto* dialog = dynamic_cast<DialogWindow*>(window);
    if (!dialog)
        return;

    WindowStateApplier::apply(*dialog, static_cast<const DialogStateManager&>(state));
}

template <>
toml::table WindowStateOperations<DialogWindow, DialogStateManager>::serializeState(
    const std::string& name, const BaseWindowState& state)
{
    return StateSerializer::serialize(name, state);
}

template <>
BaseWindowState* WindowStateOperations<DialogWindow, DialogStateManager>::getWindowState(UIWindow* window)
{
    auto* dialog = dynamic_cast<DialogWindow*>(window);
    return dialog ? &dialog->state() : nullptr;
}

template <>
std::string WindowStateOperations<DialogWindow, DialogStateManager>::getWindowName(UIWindow* window)
{
    return window->displayName();
}

template <>
void WindowStateOperations<DialogWindow, DialogStateManager>::setWindowName(
    UIWindow* window, const std::string& name)
{
    window->rename(name.c_str());
}

template <>
void WindowStateOperations<DialogWindow, DialogStateManager>::markAsDefault(UIWindow* window)
{
    auto* dialog = dynamic_cast<DialogWindow*>(window);
    if (dialog)
        registry_->markDialogAsDefault(*dialog);
}

// QuestWindow specialization
template <>
BaseWindowState* WindowStateOperations<QuestWindow, QuestStateManager>::getDefaultWindowState()
{
    if (auto* window = registry_->defaultQuestWindow())
        return &window->state();
    return nullptr;
}

template <>
BaseWindowState* WindowStateOperations<QuestWindow, QuestStateManager>::createDefaultWindow(
    const std::string& name, const BaseWindowState* initial_state)
{
    auto& window = registry_->createQuestWindow(true);
    if (!name.empty())
        window.rename(name.c_str());
    if (initial_state)
    {
        WindowStateApplier::apply(window, static_cast<const QuestStateManager&>(*initial_state));
    }
    return &window.state();
}

template <>
void WindowStateOperations<QuestWindow, QuestStateManager>::removeDefaultWindow()
{
    if (auto* window = registry_->defaultQuestWindow())
        registry_->removeWindow(window);
}

template <>
std::vector<UIWindow*> WindowStateOperations<QuestWindow, QuestStateManager>::getAllWindows()
{
    return registry_->windowsByType(UIWindowType::Quest);
}

template <>
void WindowStateOperations<QuestWindow, QuestStateManager>::applyStateToWindow(
    UIWindow* window, const BaseWindowState& state)
{
    auto* quest = dynamic_cast<QuestWindow*>(window);
    if (!quest)
        return;

    WindowStateApplier::apply(*quest, static_cast<const QuestStateManager&>(state));
}

template <>
toml::table WindowStateOperations<QuestWindow, QuestStateManager>::serializeState(
    const std::string& name, const BaseWindowState& state)
{
    return StateSerializer::serialize(name, state);
}

template <>
BaseWindowState* WindowStateOperations<QuestWindow, QuestStateManager>::getWindowState(UIWindow* window)
{
    auto* quest = dynamic_cast<QuestWindow*>(window);
    return quest ? &quest->state() : nullptr;
}

template <>
std::string WindowStateOperations<QuestWindow, QuestStateManager>::getWindowName(UIWindow* window)
{
    return window->displayName();
}

template <>
void WindowStateOperations<QuestWindow, QuestStateManager>::setWindowName(
    UIWindow* window, const std::string& name)
{
    window->rename(name.c_str());
}

template <>
void WindowStateOperations<QuestWindow, QuestStateManager>::markAsDefault(UIWindow* window)
{
    auto* quest = dynamic_cast<QuestWindow*>(window);
    if (quest)
        registry_->markQuestAsDefault(*quest);
}

// QuestHelperWindow specialization
template <>
BaseWindowState* WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::getDefaultWindowState()
{
    if (auto* window = registry_->defaultQuestHelperWindow())
        return &window->state();
    return nullptr;
}

template <>
BaseWindowState* WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::createDefaultWindow(
    const std::string& name, const BaseWindowState* initial_state)
{
    auto& window = registry_->createQuestHelperWindow(true);
    if (!name.empty())
        window.rename(name.c_str());
    if (initial_state)
    {
        WindowStateApplier::apply(window, static_cast<const QuestHelperStateManager&>(*initial_state));
    }
    return &window.state();
}

template <>
void WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::removeDefaultWindow()
{
    if (auto* window = registry_->defaultQuestHelperWindow())
        registry_->removeWindow(window);
}

template <>
std::vector<UIWindow*> WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::getAllWindows()
{
    return registry_->windowsByType(UIWindowType::QuestHelper);
}

template <>
void WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::applyStateToWindow(
    UIWindow* window, const BaseWindowState& state)
{
    auto* quest_helper = dynamic_cast<QuestHelperWindow*>(window);
    if (!quest_helper)
        return;

    WindowStateApplier::apply(*quest_helper, static_cast<const QuestHelperStateManager&>(state));
}

template <>
toml::table WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::serializeState(
    const std::string& name, const BaseWindowState& state)
{
    return StateSerializer::serialize(name, state);
}

template <>
BaseWindowState* WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::getWindowState(UIWindow* window)
{
    auto* quest_helper = dynamic_cast<QuestHelperWindow*>(window);
    return quest_helper ? &quest_helper->state() : nullptr;
}

template <>
std::string WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::getWindowName(UIWindow* window)
{
    return window->displayName();
}

template <>
void WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::setWindowName(
    UIWindow* window, const std::string& name)
{
    window->rename(name.c_str());
}

template <>
void WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>::markAsDefault(UIWindow* window)
{
    auto* quest_helper = dynamic_cast<QuestHelperWindow*>(window);
    if (quest_helper)
        registry_->markQuestHelperAsDefault(*quest_helper);
}

// Explicit template instantiations
template class WindowStateOperations<DialogWindow, DialogStateManager>;
template class WindowStateOperations<QuestWindow, QuestStateManager>;
template class WindowStateOperations<QuestHelperWindow, QuestHelperStateManager>;

