#pragma once

#include <functional>
#include <string>
#include "AppearanceSettingsPanel.hpp"
#include "TranslationSettingsPanel.hpp"
#include "DebugSettingsPanel.hpp"

class FontManager;
namespace translate { class ITranslator; }
class TranslateSession;
struct DialogStateManager;

class DialogSettingsView {
public:
    DialogSettingsView(
        DialogStateManager& state,
        FontManager& fontManager,
        TranslateSession& session
    );

    void render(
        translate::ITranslator* translator,
        std::string& applyHint,
        float& applyHintTimer,
        bool& testingConnection,
        std::string& testResult,
        std::string& testTimestamp,
        const std::string& settingsIdSuffix,
        const std::function<void()>& initTranslatorIfEnabledFn,
        const std::function<translate::ITranslator*()>& currentTranslatorFn
    );

private:
    void applyPendingResizeFlags(const AppearanceSettingsPanel::RenderResult& changes);
    
    DialogStateManager& state_;
    FontManager& fontManager_;
    TranslateSession& session_;
    AppearanceSettingsPanel appearancePanel_;
    TranslationSettingsPanel translationPanel_;
    DebugSettingsPanel debugPanel_;
};
