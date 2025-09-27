#include "WindowRegistry.hpp"

#include "DialogWindow.hpp"
#include "FontManager.hpp"

#include <imgui.h>
#include <algorithm>

// Prepares a registry capable of creating window instances.
WindowRegistry::WindowRegistry(FontManager& font_manager, ImGuiIO& io)
    : font_manager_(font_manager), io_(io)
{
}

// Registers and returns a new dialog window instance.
DialogWindow& WindowRegistry::createDialogWindow()
{
    auto dialog = std::make_unique<DialogWindow>(font_manager_, io_, dialog_counter_, makeDialogName());
    DialogWindow& ref = *dialog;
    windows_.push_back(std::move(dialog));
    ++dialog_counter_;
    return ref;
}

// Removes a window from the registry.
void WindowRegistry::removeWindow(UIWindow* window)
{
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
        [&](const std::unique_ptr<UIWindow>& entry) { return entry.get() == window; }), windows_.end());
}

// Produces a filtered view for the requested window type.
std::vector<UIWindow*> WindowRegistry::windowsByType(UIWindowType type)
{
    std::vector<UIWindow*> filtered;
    filtered.reserve(windows_.size());
    for (auto& window : windows_)
    {
        if (window && window->type() == type)
            filtered.push_back(window.get());
    }
    return filtered;
}

// Generates a sequential dialog name using alphabetic suffixes.
std::string WindowRegistry::makeDialogName()
{
    std::string suffix;
    int value = dialog_counter_;
    do
    {
        int remainder = value % 26;
        suffix.insert(suffix.begin(), static_cast<char>('A' + remainder));
        value = value / 26 - 1;
    } while (value >= 0);

    return "Dialog " + suffix;
}
