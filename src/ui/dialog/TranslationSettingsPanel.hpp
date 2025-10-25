#pragma once

#include <functional>
#include <string>

#include "../../state/TranslationConfig.hpp"

namespace translate
{
class ITranslator;
}
struct DialogStateManager;
class TranslateSession;

class TranslationSettingsPanel
{
public:
    TranslationSettingsPanel(DialogStateManager& state, TranslateSession& session);

    void render(translate::ITranslator* translator, std::string& applyHint, float& applyHintTimer,
                bool& testingConnection, std::string& testResult, std::string& testTimestamp,
                const std::function<void()>& initTranslatorIfEnabledFn,
                const std::function<translate::ITranslator*()>& currentTranslatorFn, TranslationConfig* globalConfig);

private:
    bool renderBackendSelector(TranslationConfig& config);
    bool renderBackendSpecificConfig(TranslationConfig& config);
    bool renderApplyAndTestButtons(translate::ITranslator* translator, TranslationConfig& config,
                                   std::string& applyHint, float& applyHintTimer, bool& testingConnection,
                                   std::string& testResult, std::string& testTimestamp,
                                   const std::function<void()>& initTranslatorIfEnabledFn, bool any_field_changed);
    void renderStatusAndResults(translate::ITranslator* translator, const std::string& applyHint, float applyHintTimer,
                                const std::string& testResult, const std::string& testTimestamp);

    DialogStateManager& state_;
    [[maybe_unused]] TranslateSession& session_;
    TranslationConfig* active_config_ = nullptr;
    TranslationConfig* global_config_ = nullptr;
    bool using_global_config_ = false;

    bool enable_changed_ = false;
    bool auto_apply_changed_ = false;
    bool backend_changed_ = false;
    bool lang_changed_ = false;
    bool stream_filters_changed_ = false;
    bool skip_status_frame_ = false;
    bool pending_auto_apply_ = false;
    float auto_apply_elapsed_ = 0.0f;
    bool config_dirty_pending_ = false;
};
