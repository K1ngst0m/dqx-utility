#pragma once

#include <functional>
#include <string>
#include <memory>
#include "state/QuestHelperStateManager.hpp"
#include "../dialog/AppearanceSettingsPanel.hpp"
#include "../dialog/TranslationSettingsPanel.hpp"

class FontManager;

namespace translate
{
class ITranslator;
}
class TranslateSession;

class QuestHelperSettingsView
{
public:
    explicit QuestHelperSettingsView(QuestHelperStateManager& state, FontManager& font_manager, TranslateSession& session);

    void render(translate::ITranslator* translator, std::string& apply_hint, float& apply_hint_timer,
                bool& testing_connection, std::string& test_result, std::string& test_timestamp,
                const std::string& settings_id_suffix, const std::function<void()>& initTranslatorIfEnabledFn,
                const std::function<translate::ITranslator*()>& currentTranslatorFn);

private:
    void applyPendingResizeFlags(const AppearanceSettingsPanel::RenderResult& changes);

    QuestHelperStateManager& state_;
    [[maybe_unused]] FontManager& font_manager_;
    [[maybe_unused]] TranslateSession& session_;
    AppearanceSettingsPanel appearance_panel_;
    TranslationSettingsPanel translation_panel_;
};
