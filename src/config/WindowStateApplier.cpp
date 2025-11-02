#include "WindowStateApplier.hpp"

#include "../ui/dialog/DialogWindow.hpp"
#include "../ui/quest/QuestWindow.hpp"
#include "../ui/quest/QuestHelperWindow.hpp"
#include "../ui/dialog/DialogStateManager.hpp"
#include "../ui/quest/QuestStateManager.hpp"
#include "../ui/quest/QuestHelperStateManager.hpp"
#include "../ui/common/BaseWindowState.hpp"

void WindowStateApplier::sanitizeWindowState(BaseWindowState& state)
{
    auto& ui = state.ui_state();
    ui.window_size = ImVec2(ui.width, ui.height);
    ui.pending_resize = true;
    ui.pending_reposition = true;
    ui.font = nullptr;
    ui.font_base_size = 0.0f;
}

void WindowStateApplier::apply(DialogWindow& window, const DialogStateManager& state)
{
    window.state() = state;
    window.reinitializePlaceholder();
    sanitizeWindowState(window.state());
    window.refreshFontBinding();
    window.initTranslatorIfEnabled();
}

void WindowStateApplier::apply(QuestWindow& window, const QuestStateManager& state)
{
    window.state() = state;
    window.state().quest.applyDefaults();
    window.state().translated.applyDefaults();
    window.state().original.applyDefaults();
    window.state().translation_valid = false;
    window.state().translation_failed = false;
    window.state().translation_error.clear();
    sanitizeWindowState(window.state());
    window.refreshFontBinding();
    window.initTranslatorIfEnabled();
}

void WindowStateApplier::apply(QuestHelperWindow& window, const QuestHelperStateManager& state)
{
    window.state() = state;
    window.state().quest_helper.applyDefaults();
    sanitizeWindowState(window.state());
    window.refreshFontBinding();
    window.initTranslatorIfEnabled();
}

