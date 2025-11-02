#include "TranslationSettingsPanel.hpp"

#include <cstdio>
#include <ctime>
#include <imgui.h>
#include "ui/common/BaseWindowState.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../../translate/ITranslator.hpp"
#include "../Localization.hpp"
#include "../UITheme.hpp"
#include "../../config/ConfigManager.hpp"

TranslationSettingsPanel::TranslationSettingsPanel(BaseWindowState& state, TranslateSession& session)
    : state_(state)
    , session_(session)
{
}

void TranslationSettingsPanel::render(translate::ITranslator* translator, std::string& applyHint, float& applyHintTimer,
                                      bool& testingConnection, std::string& testResult, std::string& testTimestamp,
                                      const std::function<void()>& initTranslatorIfEnabledFn,
                                      const std::function<translate::ITranslator*()>& currentTranslatorFn,
                                      TranslationConfig* globalConfig)
{
    global_config_ = globalConfig;
    using_global_config_ = (global_config_ != nullptr) && state_.use_global_translation;

    bool translator_invalidated = false;
    bool use_global_toggled = false;

    if (global_config_)
    {
        bool use_global = state_.use_global_translation;
        if (ImGui::Checkbox(i18n::get("dialog.translate.use_global"), &use_global))
        {
            state_.use_global_translation = use_global;
            translator_invalidated = true;
            use_global_toggled = true;
            if (!use_global)
            {
                state_.translation_config().copyFrom(*global_config_);
                config_dirty_pending_ = false;
            }
        }
        using_global_config_ = state_.use_global_translation;
        if (using_global_config_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", i18n::get("dialog.translate.use_global_hint"));
        }
        ImGui::Spacing();
    }
    else
    {
        state_.use_global_translation = false;
        using_global_config_ = false;
        config_dirty_pending_ = false;
    }

    active_config_ = using_global_config_ ? global_config_ : &state_.translation_config();
    TranslationConfig& config = *active_config_;

    bool selector_changed = renderBackendSelector(config);
    bool config_changed = renderBackendSpecificConfig(config);

    bool any_field_changed = enable_changed_ || auto_apply_changed_ || backend_changed_ || lang_changed_ ||
                             stream_filters_changed_ || config_changed || selector_changed;

    if (using_global_config_ && any_field_changed)
    {
        config_dirty_pending_ = true;
    }

    if (any_field_changed && !testResult.empty())
    {
        testResult.clear();
        testTimestamp.clear();
    }

    if (config.auto_apply_changes && any_field_changed)
    {
        pending_auto_apply_ = true;
        auto_apply_elapsed_ = 0.0f;
        applyHint.clear();
        applyHintTimer = 0.0f;
    }
    else if (!config.auto_apply_changes)
    {
        pending_auto_apply_ = false;
        auto_apply_elapsed_ = 0.0f;
    }

    ImGui::Spacing();

    translator_invalidated |=
        renderApplyAndTestButtons(translator, config, applyHint, applyHintTimer, testingConnection, testResult,
                                  testTimestamp, initTranslatorIfEnabledFn, any_field_changed);

    if (applyHintTimer > 0.0f)
    {
        applyHintTimer -= ImGui::GetIO().DeltaTime;
        if (applyHintTimer <= 0.0f)
        {
            applyHintTimer = 0.0f;
            applyHint.clear();
        }
    }

    if (config.auto_apply_changes && pending_auto_apply_)
    {
        auto_apply_elapsed_ += ImGui::GetIO().DeltaTime;
        if (auto_apply_elapsed_ >= 0.5f)
        {
            pending_auto_apply_ = false;
            auto_apply_elapsed_ = 0.0f;
            initTranslatorIfEnabledFn();
            applyHint = i18n::get("dialog.settings.apply_hint");
            applyHintTimer = 3.0f;
            translator_invalidated = true;
        }
    }

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

        if (using_global_config_ && config_dirty_pending_ && !use_global_toggled)
        {
            if (auto* cm = ConfigManager_Get())
            {
                cm->markGlobalTranslationDirty();
            }
            config_dirty_pending_ = false;
        }
    }

    if (skip_status_frame_)
    {
        skip_status_frame_ = false;
        return;
    }

    renderStatusAndResults(translator, applyHint, applyHintTimer, testResult, testTimestamp);
}

bool TranslationSettingsPanel::renderBackendSelector(TranslationConfig& config)
{
    stream_filters_changed_ = false;
    enable_changed_ = ImGui::Checkbox(i18n::get("dialog.translate.enable"), &config.translate_enabled);
    auto_apply_changed_ = ImGui::Checkbox(i18n::get("dialog.translate.auto_apply"), &config.auto_apply_changes);
    ImGui::Spacing();

    bool include_dialog_changed =
        ImGui::Checkbox(i18n::get("dialog.translate.include_dialog"), &config.include_dialog_stream);
    bool include_corner_changed =
        ImGui::Checkbox(i18n::get("dialog.translate.include_corner"), &config.include_corner_stream);
    bool glossary_changed = ImGui::Checkbox(i18n::get("dialog.translate.use_glossary"), &config.glossary_enabled);
    stream_filters_changed_ = include_dialog_changed || include_corner_changed || glossary_changed;
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.translate.backend.label"));
    const char* backend_items[] = {
        i18n::get("dialog.translate.backend.items.openai_compat"), i18n::get("dialog.translate.backend.items.google"),
        i18n::get("dialog.translate.backend.items.glm4_zhipu"),    i18n::get("dialog.translate.backend.items.qwen_mt"),
        i18n::get("dialog.translate.backend.items.niutrans"),      i18n::get("dialog.translate.backend.items.youdao"),
        i18n::get("dialog.translate.backend.items.freellm")
    };
    int current_backend = static_cast<int>(config.translation_backend);
    ImGui::SetNextItemWidth(220.0f);
    backend_changed_ =
        ImGui::Combo("##translation_backend", &current_backend, backend_items, IM_ARRAYSIZE(backend_items));
    if (backend_changed_)
    {
        config.translation_backend = static_cast<TranslationConfig::TranslationBackend>(current_backend);
    }

    ImGui::TextUnformatted(i18n::get("dialog.settings.target_language"));
    const char* lang_items[] = { i18n::get("dialog.settings.target_lang.en_us"),
                                 i18n::get("dialog.settings.target_lang.zh_cn"),
                                 i18n::get("dialog.settings.target_lang.zh_tw") };
    int current_lang = static_cast<int>(config.target_lang_enum);
    ImGui::SetNextItemWidth(220.0f);
    lang_changed_ = ImGui::Combo("##target_lang", &current_lang, lang_items, IM_ARRAYSIZE(lang_items));
    if (lang_changed_)
    {
        config.target_lang_enum = static_cast<TranslationConfig::TargetLang>(current_lang);
    }

    return enable_changed_ || auto_apply_changed_ || backend_changed_ || lang_changed_ || stream_filters_changed_;
}

bool TranslationSettingsPanel::renderBackendSpecificConfig(TranslationConfig& config)
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
    bool prompt_changed = false;

    if (config.translation_backend == TranslationConfig::TranslationBackend::OpenAI ||
        config.translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM ||
        config.translation_backend == TranslationConfig::TranslationBackend::FreeLLM)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted(i18n::get("dialog.settings.system_prompt"));
        ImGui::TextDisabled("%s", i18n::get("dialog.settings.system_prompt_hint"));

        prompt_changed = ImGui::InputTextMultiline("##prompt", config.custom_prompt.data(), config.custom_prompt.size(),
                                                   ImVec2(500.0f, 200.0f),
                                                   ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_AllowTabInput);

        if (ImGui::Button(i18n::get("dialog.settings.reset_prompt")))
        {
            std::snprintf(config.custom_prompt.data(), config.custom_prompt.size(), "%s",
                          i18n::get("dialog.settings.default_prompt"));
            prompt_changed = true;
        }

        ImGui::Spacing();
    }

    if (config.translation_backend == TranslationConfig::TranslationBackend::OpenAI)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.base_url"));
        ImGui::SetNextItemWidth(300.0f);
        base_url_changed =
            ImGui::InputText("##openai_base", config.openai_base_url.data(), config.openai_base_url.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
        ImGui::SetNextItemWidth(300.0f);
        model_changed = ImGui::InputText("##openai_model", config.openai_model.data(), config.openai_model.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        openai_key_changed = ImGui::InputText("##openai_key", config.openai_api_key.data(),
                                              config.openai_api_key.size(), ImGuiInputTextFlags_Password);
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::Google)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key_optional"));
        ImGui::SetNextItemWidth(300.0f);
        google_key_changed = ImGui::InputText("##google_key", config.google_api_key.data(),
                                              config.google_api_key.size(), ImGuiInputTextFlags_Password);
        ImGui::TextDisabled("%s", i18n::get("dialog.settings.google_note"));
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        zhipu_key_changed = ImGui::InputText("##zhipu_key", config.zhipu_api_key.data(), config.zhipu_api_key.size(),
                                             ImGuiInputTextFlags_Password);
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::QwenMT)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
        ImGui::SetNextItemWidth(300.0f);
        int qidx = 1;
        if (std::string(config.qwen_model.data()).find("qwen-mt-plus") == 0)
            qidx = 0;
        const char* qwen_models[] = { "qwen-mt-plus", "qwen-mt-turbo" };
        if (ImGui::Combo("##qwen_model", &qidx, qwen_models, IM_ARRAYSIZE(qwen_models)))
        {
            const char* sel = qwen_models[qidx];
            std::snprintf(config.qwen_model.data(), config.qwen_model.size(), "%s", sel);
            model_changed = true;
        }

        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        qwen_key_changed = ImGui::InputText("##qwen_key", config.qwen_api_key.data(), config.qwen_api_key.size(),
                                            ImGuiInputTextFlags_Password);
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::Niutrans)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
        ImGui::SetNextItemWidth(300.0f);
        niutrans_key_changed = ImGui::InputText("##niutrans_key", config.niutrans_api_key.data(),
                                                config.niutrans_api_key.size(), ImGuiInputTextFlags_Password);
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::Youdao)
    {
        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_app_key"));
        ImGui::SetNextItemWidth(300.0f);
        youdao_app_key_changed =
            ImGui::InputText("##youdao_app_key", config.youdao_app_key.data(), config.youdao_app_key.size());

        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_app_secret"));
        ImGui::SetNextItemWidth(300.0f);
        youdao_app_secret_changed = ImGui::InputText("##youdao_app_secret", config.youdao_app_secret.data(),
                                                     config.youdao_app_secret.size(), ImGuiInputTextFlags_Password);

        ImGui::TextUnformatted(i18n::get("dialog.settings.youdao_mode_label"));
        const char* mode_items[] = { i18n::get("dialog.settings.youdao_mode_text"),
                                     i18n::get("dialog.settings.youdao_mode_large") };
        int current_mode = static_cast<int>(config.youdao_mode);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##youdao_mode", &current_mode, mode_items, IM_ARRAYSIZE(mode_items)))
        {
            config.youdao_mode = (current_mode == static_cast<int>(TranslationConfig::YoudaoMode::LargeModel)) ?
                                     TranslationConfig::YoudaoMode::LargeModel :
                                     TranslationConfig::YoudaoMode::Text;
            youdao_mode_changed = true;
        }
    }
    else if (config.translation_backend == TranslationConfig::TranslationBackend::FreeLLM)
    {
        // FreeLLM backend - only show model dropdown (no API keys/URLs needed)
        ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
        ImGui::SetNextItemWidth(300.0f);

        int model_idx = 0;
        if (std::string(config.freellm_model.data()).find("ep-w8sv4r") == 0)
            model_idx = 1; // DeepSeek
        else
            model_idx = 0; // Qwen (default)

        const char* freellm_models[] = { "Qwen", "DeepSeek" };
        if (ImGui::Combo("##freellm_model", &model_idx, freellm_models, IM_ARRAYSIZE(freellm_models)))
        {
            const char* selected_model_id = (model_idx == 0)
                ? "ep-c193qt-1761835797295793905"  // Qwen
                : "ep-w8sv4r-1761835960223672978"; // DeepSeek
            std::snprintf(config.freellm_model.data(), config.freellm_model.size(), "%s", selected_model_id);
            model_changed = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", i18n::get("dialog.settings.freellm_note"));
    }

    return base_url_changed || model_changed || openai_key_changed || google_key_changed || zhipu_key_changed ||
           qwen_key_changed || niutrans_key_changed || youdao_app_key_changed || youdao_app_secret_changed ||
           youdao_mode_changed || prompt_changed;
}

bool TranslationSettingsPanel::renderApplyAndTestButtons(translate::ITranslator* translator, TranslationConfig& config,
                                                         std::string& applyHint, float& applyHintTimer,
                                                         bool& testingConnection, std::string& testResult,
                                                         std::string& testTimestamp,
                                                         const std::function<void()>& initTranslatorIfEnabledFn,
                                                         bool any_field_changed)
{
    (void)any_field_changed;
    (void)translator;
    bool translator_invalidated = false;

    if (!config.auto_apply_changes)
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

        translate::BackendConfig test_cfg;
        test_cfg.backend = static_cast<translate::Backend>(config.translation_backend);
        switch (config.target_lang_enum)
        {
        case TranslationConfig::TargetLang::EN_US:
            test_cfg.target_lang = "en-us";
            break;
        case TranslationConfig::TargetLang::ZH_CN:
            test_cfg.target_lang = "zh-cn";
            break;
        case TranslationConfig::TargetLang::ZH_TW:
            test_cfg.target_lang = "zh-tw";
            break;
        }
        if (config.translation_backend == TranslationConfig::TranslationBackend::OpenAI)
        {
            test_cfg.base_url = config.openai_base_url.data();
            test_cfg.model = config.openai_model.data();
            test_cfg.api_key = config.openai_api_key.data();
        }
        else if (config.translation_backend == TranslationConfig::TranslationBackend::Google)
        {
            test_cfg.base_url.clear();
            test_cfg.model.clear();
            test_cfg.api_key = config.google_api_key.data();
        }
        else if (config.translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
        {
            test_cfg.base_url = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
            test_cfg.model = "glm-4-flash";
            test_cfg.api_key = config.zhipu_api_key.data();
        }
        else if (config.translation_backend == TranslationConfig::TranslationBackend::QwenMT)
        {
            test_cfg.base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
            const char* qm = config.qwen_model.data();
            test_cfg.model = (qm && qm[0]) ? qm : "qwen-mt-turbo";
            test_cfg.api_key = config.qwen_api_key.data();
        }
        else if (config.translation_backend == TranslationConfig::TranslationBackend::Niutrans)
        {
            test_cfg.base_url = "https://api.niutrans.com/NiuTransServer/translation";
            test_cfg.model.clear();
            test_cfg.api_key = config.niutrans_api_key.data();
        }
        else if (config.translation_backend == TranslationConfig::TranslationBackend::Youdao)
        {
            if (config.youdao_mode == TranslationConfig::YoudaoMode::LargeModel)
            {
                test_cfg.base_url = "https://openapi.youdao.com/llm_trans";
                test_cfg.model = "youdao_large";
            }
            else
            {
                test_cfg.base_url = "https://openapi.youdao.com/api";
                test_cfg.model = "youdao_text";
            }
            test_cfg.api_key = config.youdao_app_key.data();
            test_cfg.api_secret = config.youdao_app_secret.data();
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
        if (temp_translator)
            temp_translator->shutdown();

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

void TranslationSettingsPanel::renderStatusAndResults(translate::ITranslator* translator, const std::string& applyHint,
                                                      float applyHintTimer, const std::string& testResult,
                                                      const std::string& testTimestamp)
{
    const char* status = (translator && translator->isReady()) ? i18n::get("dialog.settings.ready") :
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
            std::string line =
                i18n::format("dialog.settings.test_result", {
                                                                { "time", testTimestamp },
                                                                { "text", testResult    }
            });
            ImGui::TextColored(color, "%s", line.c_str());
        }
        else
        {
            std::string line = i18n::format("dialog.settings.test_result_no_time", {
                                                                                       { "text", testResult }
            });
            ImGui::TextColored(color, "%s", line.c_str());
        }
    }
}
