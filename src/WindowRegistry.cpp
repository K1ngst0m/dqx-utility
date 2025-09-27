#include "WindowRegistry.hpp"

#include "DialogWindow.hpp"
#include "FontManager.hpp"

#include <imgui.h>

// Prepares a registry capable of creating window instances.
WindowRegistry::WindowRegistry(FontManager& font_manager, ImGuiIO& io)
    : font_manager_(font_manager), io_(io)
{
}

// Registers and returns a new dialog window instance.
DialogWindow& WindowRegistry::createDialogWindow()
{
    auto dialog = std::make_unique<DialogWindow>(font_manager_, io_, dialog_counter_++);
    DialogWindow& ref = *dialog;
    windows_.push_back(std::move(dialog));
    return ref;
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
