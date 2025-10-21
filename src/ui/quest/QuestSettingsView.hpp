#pragma once

#include <functional>
#include <string>

#include "../dialog/AppearanceSettingsPanel.hpp"
#include "../dialog/TranslationSettingsPanel.hpp"

class FontManager;

namespace translate
{
class ITranslator;
}
class TranslateSession;
struct QuestStateManager;

class QuestSettingsView
{
public:
    QuestSettingsView(QuestStateManager& state, FontManager& font_manager, TranslateSession& session);

    void render(translate::ITranslator* translator, std::string& apply_hint, float& apply_hint_timer,
                bool& testing_connection, std::string& test_result, std::string& test_timestamp,
                const std::string& settings_id_suffix, const std::function<void()>& initTranslatorIfEnabledFn,
                const std::function<translate::ITranslator*()>& currentTranslatorFn);

private:
    void applyPendingResizeFlags(const AppearanceSettingsPanel::RenderResult& changes);

    QuestStateManager& state_;
    FontManager& font_manager_;
    TranslateSession& session_;
    AppearanceSettingsPanel appearance_panel_;
    TranslationSettingsPanel translation_panel_;
};
