#include "DialogSettingsView.hpp"

#include <imgui.h>
#include "../UITheme.hpp"
#include "../Localization.hpp"
#include "../../state/DialogStateManager.hpp"
#include "../FontManager.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../translate/ITranslator.hpp"
#include "../../translate/TranslateSession.hpp"

DialogSettingsView::DialogSettingsView(
    DialogStateManager& state,
    FontManager& fontManager,
    TranslateSession& session
)
    : state_(state)
    , fontManager_(fontManager)
    , session_(session)
    , appearancePanel_(state)
    , translationPanel_(state, session)
    , debugPanel_(state, fontManager, session)
{
}

void DialogSettingsView::render(
    translate::ITranslator* translator,
    std::string& applyHint,
    float& applyHintTimer,
    bool& testingConnection,
    std::string& testResult,
    std::string& testTimestamp,
    const std::string& settingsIdSuffix,
    const std::function<void()>& initTranslatorIfEnabledFn,
    const std::function<translate::ITranslator*()>& currentTranslatorFn
)
{
    ImGui::Spacing();

    if (ImGui::Button(i18n::get("dialog.settings.save_config")))
    {
        extern bool ConfigManager_SaveAll();
        bool ok = ConfigManager_SaveAll();
        if (!ok)
        {
            ImGui::SameLine();
            ImGui::TextColored(UITheme::warningColor(), "%s", i18n::get("dialog.settings.save_config_failed"));
        }
    }
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(i18n::get("dialog.appearance.title")))
    {
        ImGui::Indent();
        auto changes = appearancePanel_.render();
        ImGui::Unindent();
        ImGui::Spacing();
        applyPendingResizeFlags(changes);
    }

    if (ImGui::CollapsingHeader(i18n::get("dialog.translate.title"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        TranslationConfig* global_config = nullptr;
        if (auto* cm = ConfigManager_Get())
        {
            global_config = &cm->globalTranslationConfig();
        }
        translationPanel_.render(translator,
                                 applyHint,
                                 applyHintTimer,
                                 testingConnection,
                                 testResult,
                                 testTimestamp,
                                 initTranslatorIfEnabledFn,
                                 currentTranslatorFn,
                                 global_config);
        ImGui::Unindent();
        ImGui::Spacing();
    }

    #if DQXU_ENABLE_DEBUG_SECTIONS
    if (ImGui::CollapsingHeader(i18n::get("dialog.debug.title")))
    {
        ImGui::Indent();
        debugPanel_.render(settingsIdSuffix);
        ImGui::Unindent();
        ImGui::Spacing();
    }
    #endif

}

void DialogSettingsView::applyPendingResizeFlags(const AppearanceSettingsPanel::RenderResult& changes)
{
    if (changes.width_changed)
    {
        state_.ui_state().window_size.x = state_.ui_state().width;
        state_.ui_state().pending_resize = true;
    }
    if (changes.height_changed)
    {
        state_.ui_state().window_size.y = state_.ui_state().height;
        state_.ui_state().pending_resize = true;
    }
    if (changes.alpha_changed || changes.font_changed)
    {
        state_.ui_state().pending_resize = state_.ui_state().pending_resize;
    }
}
