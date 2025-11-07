#include "QuestHelperSettingsView.hpp"

#include <imgui.h>

#include "../Localization.hpp"
#include "../UITheme.hpp"
#include "QuestHelperStateManager.hpp"
#include "../FontManager.hpp"
#include "../../config/ConfigManager.hpp"
#include "../GlobalStateManager.hpp"
#include "../../app/Application.hpp"
#include "../../translate/ITranslator.hpp"
#include "../../translate/TranslateSession.hpp"

QuestHelperSettingsView::QuestHelperSettingsView(QuestHelperStateManager& state, FontManager& font_manager, TranslateSession& session, ConfigManager& config, GlobalStateManager& global_state)
    : state_(state)
    , font_manager_(font_manager)
    , session_(session)
    , config_(config)
    , global_state_(global_state)
    , appearance_panel_(state)
    , translation_panel_(state, session, config, global_state)
{
}

void QuestHelperSettingsView::render(translate::ITranslator* translator, std::string& apply_hint, float& apply_hint_timer,
                                      bool& testing_connection, std::string& test_result, std::string& test_timestamp,
                                      const std::string& settings_id_suffix,
                                      const std::function<void()>& initTranslatorIfEnabledFn,
                                      const std::function<translate::ITranslator*()>& currentTranslatorFn)
{
    ImGui::PushID(settings_id_suffix.c_str());
    ImGui::Spacing();

    if (ImGui::Button(i18n::get("dialog.settings.save_config")))
    {
        config_.save();
    }
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(i18n::get("dialog.appearance.title")))
    {
        ImGui::Indent();
        auto changes = appearance_panel_.render();
        ImGui::Unindent();
        ImGui::Spacing();
        applyPendingResizeFlags(changes);
    }

    if (ImGui::CollapsingHeader(i18n::get("dialog.translate.title"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        TranslationConfig* global_config = &global_state_.translationConfig();
        translation_panel_.render(translator, apply_hint, apply_hint_timer, testing_connection, test_result,
                                  test_timestamp, initTranslatorIfEnabledFn, currentTranslatorFn, global_config);
        ImGui::Unindent();
        ImGui::Spacing();
    }

    ImGui::PopID();
}

void QuestHelperSettingsView::applyPendingResizeFlags(const AppearanceSettingsPanel::RenderResult& changes)
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
}
