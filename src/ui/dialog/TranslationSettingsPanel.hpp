#pragma once

#include <functional>
#include <string>

namespace translate { class ITranslator; }
struct DialogStateManager;
class TranslateSession;

class TranslationSettingsPanel {
public:
    TranslationSettingsPanel(
        DialogStateManager& state,
        TranslateSession& session
    );
    
    void render(
        translate::ITranslator* translator,
        std::string& applyHint,
        float& applyHintTimer,
        bool& testingConnection,
        std::string& testResult,
        std::string& testTimestamp,
        const std::function<void()>& initTranslatorIfEnabledFn,
        const std::function<translate::ITranslator*()>& currentTranslatorFn
    );

private:
    bool renderBackendSelector();
    bool renderBackendSpecificConfig();
    bool renderApplyAndTestButtons(
        translate::ITranslator* translator,
        std::string& applyHint,
        float& applyHintTimer,
        bool& testingConnection,
        std::string& testResult,
        std::string& testTimestamp,
        const std::function<void()>& initTranslatorIfEnabledFn,
        bool any_field_changed
    );
    void renderStatusAndResults(
        translate::ITranslator* translator,
        const std::string& applyHint,
        float applyHintTimer,
        const std::string& testResult,
        const std::string& testTimestamp
    );
    
    DialogStateManager& state_;
    TranslateSession& session_;
    
    bool enable_changed_ = false;
    bool auto_apply_changed_ = false;
    bool backend_changed_ = false;
    bool lang_changed_ = false;
    bool skip_status_frame_ = false;
};
