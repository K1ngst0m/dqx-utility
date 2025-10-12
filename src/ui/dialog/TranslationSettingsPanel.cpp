#include "TranslationSettingsPanel.hpp"

#include <cstdio>
#include <ctime>
#include <imgui.h>
#include "../../state/DialogStateManager.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../../translate/ITranslator.hpp"
#include "../Localization.hpp"
#include "../UITheme.hpp"

TranslationSettingsPanel::TranslationSettingsPanel(
    DialogStateManager& state,
    TranslateSession& session
)
    : state_(state)
    , session_(session)
{
}

void TranslationSettingsPanel::render(
    translate::ITranslator* translator,
    std::string& applyHint,
    float& applyHintTimer,
    bool& testingConnection,
    std::string& testResult,
    std::string& testTimestamp,
    const std::function<void()>& initTranslatorIfEnabledFn,
    const std::function<translate::ITranslator*()>& currentTranslatorFn
)
{
    bool selector_changed = renderBackendSelector();
    bool config_changed = renderBackendSpecificConfig();
    
    bool any_field_changed = enable_changed_ || auto_apply_changed_ || backend_changed_ || 
                             lang_changed_ || config_changed || selector_changed;
    bool translator_invalidated = false;
    
    if (any_field_changed && !testResult.empty())
    {
        testResult.clear();
        testTimestamp.clear();
    }
    
    if (state_.translation_config().auto_apply_changes && any_field_changed)
    {
        initTranslatorIfEnabledFn();
        applyHint = i18n::get("dialog.settings.apply_hint");
        applyHintTimer = 5.0f;
        translator_invalidated = true;
    }
    
    ImGui::Spacing();
    
    translator_invalidated |= renderApplyAndTestButtons(translator, applyHint, applyHintTimer, testingConnection, 
                              testResult, testTimestamp, initTranslatorIfEnabledFn, any_field_changed);

    if (translator_invalidated)
    {
        skip_status_frame_ = true;
        if (currentTranslatorFn)
        {
            translator = currentTranslatorFn();
        }
        else
        {
            translator = nullptr;
        }
    }

    if (skip_status_frame_)
    {
        skip_status_frame_ = false;
        return;
    }

    renderStatusAndResults(translator, applyHint, applyHintTimer, testResult, testTimestamp);
}

bool TranslationSettingsPanel::renderBackendSelector()
{
    enable_changed_ = ImGui::Checkbox(i18n::get("dialog.translate.enable"), &state_.translation_config().translate_enabled);
    auto_apply_changed_ = ImGui::Checkbox(i18n::get("dialog.translate.auto_apply"), &state_.translation_config().auto_apply_changes);
    ImGui::Spacing();
    
    ImGui::TextUnformatted(i18n::get("dialog.translate.backend.label"));
    const char* backend_items[] = {
        i18n::get("dialog.translate.backend.items.openai_compat"),
        i18n::get("dialog.translate.backend.items.google"),
        i18n::get("dialog.translate.backend.items.glm4_zhipu"),
        i18n::get("dialog.translate.backend.items.qwen_mt"),
        i18n::get("dialog.translate.backend.items.niutrans"),
        i18n::get("dialog.translate.backend.items.youdao")
    };
    int current_backend = static_cast<int>(state_.translation_config().translation_backend);
    ImGui::SetNextItemWidth(220.0f);
    backend_changed_ = ImGui::Combo("##translation_backend", &current_backend, backend_items, IM_ARRAYSIZE(backend_items));
    if (backend_changed_)
    {
        state_.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(current_backend);
    }
    
    ImGui::TextUnformatted(i18n::get("dialog.settings.target_language"));
    const char* lang_items[] = {
        i18n::get("dialog.settings.target_lang.en_us"),
        i18n::get("dialog.settings.target_lang.zh_cn"),
        i18n::get("dialog.settings.target_lang.zh_tw")
    };
    int current_lang = static_cast<int>(state_.translation_config().target_lang_enum);
    ImGui::SetNextItemWidth(220.0f);
    lang_changed_ = ImGui::Combo("##target_lang", &current_lang, lang_items, IM_ARRAYSIZE(lang_items));
    if (lang_changed_)
    {
        state_.translation_config().target_lang_enum = static_cast<TranslationConfig::TargetLang>(current_lang);
    }
    
    return enable_changed_ || auto_apply_changed_ || backend_changed_ || lang_changed_;
}

bool TranslationSettingsPanel::renderBackendSpecificConfig()
{
    bool base_url_changed = false;
    bool model_changed = false;
    bool openai_key_changed = false;
    bool google_key_changed = false;
    bool zhipu_key_changed = false;
    bool qwen_key_changed = false;
    bool niutrans_key_changed = false;
    bool youdao_app_key_changed = false;
    bool youdao_app_secret_changed = false;
    bool youdao_mode_changed = false;
    
    if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.base_url"));
        ImGui::SetNextItemWidth(300.0f);
        base_url_changed = ImGui::InputText("##openai_base", 
                                            state_.translation_config().openai_base_url.data(), 
                                            state_.translation_config().openai_base_url.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
        ImGui::SetNextItemWidth(300.0f);
        model_changed = ImGui::InputText("##openai_model", 
                                        state_.translation_config().openai_model.data(), 
                                        state_.translation_config().openai_model.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        openai_key_changed = ImGui::InputText("##openai_key", 
                                             state_.translation_config().openai_api_key.data(), 
                                             state_.translation_config().openai_api_key.size(), 
                                             ImGuiInputTextFlags_Password);
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key_optional"));
        ImGui::SetNextItemWidth(300.0f);
        google_key_changed = ImGui::InputText("##google_key", 
                                             state_.translation_config().google_api_key.data(), 
                                             state_.translation_config().google_api_key.size(), 
                                             ImGuiInputTextFlags_Password);
        ImGui::TextDisabled("%s", i18n::get("dialog.settings.google_note"));
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        zhipu_key_changed = ImGui::InputText("##zhipu_key", 
                                            state_.translation_config().zhipu_api_key.data(), 
                                            state_.translation_config().zhipu_api_key.size(), 
                                            ImGuiInputTextFlags_Password);
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::QwenMT)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
        ImGui::SetNextItemWidth(300.0f);
        int qidx = 1;
        if (std::string(state_.translation_config().qwen_model.data()).find("qwen-mt-plus") == 0) qidx = 0;
        const char* qwen_models[] = { "qwen-mt-plus", "qwen-mt-turbo" };
        if (ImGui::Combo("##qwen_model", &qidx, qwen_models, IM_ARRAYSIZE(qwen_models)))
        {
            const char* sel = qwen_models[qidx];
            std::snprintf(state_.translation_config().qwen_model.data(), 
                         state_.translation_config().qwen_model.size(), 
                         "%s", sel);
            model_changed = true;
        }

        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        qwen_key_changed = ImGui::InputText("##qwen_key", 
                                           state_.translation_config().qwen_api_key.data(), 
                                           state_.translation_config().qwen_api_key.size(), 
                                           ImGuiInputTextFlags_Password);
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Niutrans)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        niutrans_key_changed = ImGui::InputText("##niutrans_key", 
                                               state_.translation_config().niutrans_api_key.data(), 
                                               state_.translation_config().niutrans_api_key.size(), 
                                               ImGuiInputTextFlags_Password);
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Youdao)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_app_key"));
        ImGui::SetNextItemWidth(300.0f);
        youdao_app_key_changed = ImGui::InputText("##youdao_app_key",
                                                 state_.translation_config().youdao_app_key.data(),
                                                 state_.translation_config().youdao_app_key.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_app_secret"));
        ImGui::SetNextItemWidth(300.0f);
        youdao_app_secret_changed = ImGui::InputText("##youdao_app_secret",
                                                    state_.translation_config().youdao_app_secret.data(),
                                                    state_.translation_config().youdao_app_secret.size(),
                                                    ImGuiInputTextFlags_Password);

        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_mode_label"));
        const char* mode_items[] = {
            i18n::get("dialog.settings.youdao_mode_text"),
            i18n::get("dialog.settings.youdao_mode_large")
        };
        int current_mode = static_cast<int>(state_.translation_config().youdao_mode);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##youdao_mode", &current_mode, mode_items, IM_ARRAYSIZE(mode_items)))
        {
            state_.translation_config().youdao_mode = (current_mode == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel))
                ? TranslationConfig::YoudaoMode::LargeModel
                : TranslationConfig::YoudaoMode::Text;
            youdao_mode_changed = true;
        }
    }
    
    return base_url_changed || model_changed || openai_key_changed || google_key_changed || 
           zhipu_key_changed || qwen_key_changed || niutrans_key_changed ||
           youdao_app_key_changed || youdao_app_secret_changed || youdao_mode_changed;
}

bool TranslationSettingsPanel::renderApplyAndTestButtons(
    translate::ITranslator* translator,
    std::string& applyHint,
    float& applyHintTimer,
    bool& testingConnection,
    std::string& testResult,
    std::string& testTimestamp,
    const std::function<void()>& initTranslatorIfEnabledFn,
    bool any_field_changed
)
{
    (void)any_field_changed;
    (void)translator;
    bool translator_invalidated = false;
    
    if (!state_.translation_config().auto_apply_changes)
    {
        if (ImGui::Button(i18n::get("common.apply")))
        {
            initTranslatorIfEnabledFn();
            applyHint = i18n::get("dialog.settings.apply_hint");
            applyHintTimer = 5.0f;
            translator_invalidated = true;
        }
        ImGui::SameLine();
    }
    
    if (ImGui::Button(i18n::get("dialog.settings.test")) && !testingConnection)
    {
        testingConnection = true;
        testResult = i18n::get("dialog.settings.testing");
        
        translate::TranslatorConfig test_cfg;
        test_cfg.backend = static_cast<translate::Backend>(state_.translation_config().translation_backend);
        switch (state_.translation_config().target_lang_enum)
        {
        case TranslationConfig::TargetLang::EN_US: test_cfg.target_lang = "en-us"; break;
        case TranslationConfig::TargetLang::ZH_CN: test_cfg.target_lang = "zh-cn"; break;
        case TranslationConfig::TargetLang::ZH_TW: test_cfg.target_lang = "zh-tw"; break;
        }
        if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
        {
            test_cfg.base_url = state_.translation_config().openai_base_url.data();
            test_cfg.model = state_.translation_config().openai_model.data();
            test_cfg.api_key = state_.translation_config().openai_api_key.data();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
        {
            test_cfg.base_url.clear();
            test_cfg.model.clear();
            test_cfg.api_key = state_.translation_config().google_api_key.data();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
        {
            test_cfg.base_url = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
            test_cfg.model = "glm-4-flash";
            test_cfg.api_key = state_.translation_config().zhipu_api_key.data();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::QwenMT)
        {
            test_cfg.base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
            const char* qm = state_.translation_config().qwen_model.data();
            test_cfg.model = (qm && qm[0]) ? qm : "qwen-mt-turbo";
            test_cfg.api_key = state_.translation_config().qwen_api_key.data();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Niutrans)
        {
            test_cfg.base_url = "https://api.niutrans.com/NiuTransServer/translation";
            test_cfg.model.clear();
            test_cfg.api_key = state_.translation_config().niutrans_api_key.data();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Youdao)
        {
            if (state_.translation_config().youdao_mode == TranslationConfig::YoudaoMode::LargeModel)
            {
                test_cfg.base_url = "https://openapi.youdao.com/llm_trans";
                test_cfg.model = "youdao_large";
            }
            else
            {
                test_cfg.base_url = "https://openapi.youdao.com/api";
                test_cfg.model = "youdao_text";
            }
            test_cfg.api_key = state_.translation_config().youdao_app_key.data();
            test_cfg.api_secret = state_.translation_config().youdao_app_secret.data();
        }
        
        auto temp_translator = translate::createTranslator(test_cfg.backend);
        if (temp_translator && temp_translator->init(test_cfg))
        {
            testResult = temp_translator->testConnection();
        }
        else
        {
            testResult = "Error: Failed to initialize translator for testing";
        }
        if (temp_translator) temp_translator->shutdown();
        
        std::time_t now = std::time(nullptr);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &now);
#else
        localtime_r(&now, &tm_buf);
#endif
        char time_str[16];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
        testTimestamp = time_str;
        
        testingConnection = false;
    }

    return translator_invalidated;
}

void TranslationSettingsPanel::renderStatusAndResults(
    translate::ITranslator* translator,
    const std::string& applyHint,
    float applyHintTimer,
    const std::string& testResult,
    const std::string& testTimestamp
)
{
    const char* status = (translator && translator->isReady()) ? 
                         i18n::get("dialog.settings.ready") : 
                         i18n::get("dialog.settings.not_ready");
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s", i18n::get("dialog.settings.status_label"), status);

    if (applyHintTimer > 0.0f && !applyHint.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "%s", applyHint.c_str());
    }
    
    if (translator)
    {
        const char* err = translator->lastError();
        if (err && err[0]) 
        {
            ImGui::TextColored(UITheme::warningColor(), "%s", err);
        }
    }
    
    if (!testResult.empty())
    {
        ImVec4 color;
        if (testResult.find("Success:") == 0)
            color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
        else if (testResult.find("Warning:") == 0)
            color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        else if (testResult.find("Error:") == 0 || testResult.find("Testing") == 0)
            color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
        else
            color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        
        if (!testTimestamp.empty())
        {
            std::string line = i18n::format("dialog.settings.test_result", 
                                           {{"time", testTimestamp}, {"text", testResult}});
            ImGui::TextColored(color, "%s", line.c_str());
        }
        else
        {
            std::string line = i18n::format("dialog.settings.test_result_no_time", 
                                           {{"text", testResult}});
            ImGui::TextColored(color, "%s", line.c_str());
        }
    }
}
