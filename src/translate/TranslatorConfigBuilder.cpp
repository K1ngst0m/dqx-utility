#include "ITranslator.hpp"
#include "../state/TranslationConfig.hpp"
#include <algorithm>

namespace translate
{

static std::string to_string_lang(::TranslationConfig::TargetLang t)
{
    switch (t)
    {
    case ::TranslationConfig::TargetLang::EN_US:
        return "en-us";
    case ::TranslationConfig::TargetLang::ZH_CN:
        return "zh-cn";
    case ::TranslationConfig::TargetLang::ZH_TW:
        return "zh-tw";
    }
    return "en-us";
}

static std::string safe_str(const std::array<char, ::TranslationConfig::URLSize>& a) { return a.data(); }

static std::string safe_model(const std::array<char, ::TranslationConfig::ModelSize>& a) { return a.data(); }

static std::string safe_key(const std::array<char, ::TranslationConfig::ApiKeySize>& a) { return a.data(); }

BackendConfig BackendConfig::from(const ::TranslationConfig& cfg_ui)
{
    BackendConfig out;
    out.backend = static_cast<Backend>(cfg_ui.translation_backend);
    out.target_lang = to_string_lang(cfg_ui.target_lang_enum);
    out.prompt = cfg_ui.custom_prompt.data();
    out.max_concurrent_requests =
        cfg_ui.max_concurrent_requests <= 0 ? 1 : static_cast<std::size_t>(cfg_ui.max_concurrent_requests);
    out.request_interval_seconds =
        cfg_ui.request_interval_seconds < 0.f ? 0.0 : static_cast<double>(cfg_ui.request_interval_seconds);
    out.max_retries = cfg_ui.max_retries < 0 ? 0 : cfg_ui.max_retries;

    switch (cfg_ui.translation_backend)
    {
    case ::TranslationConfig::TranslationBackend::OpenAI:
    {
        out.base_url = safe_str(cfg_ui.openai_base_url);
        out.model = safe_model(cfg_ui.openai_model);
        out.api_key = safe_key(cfg_ui.openai_api_key);
        break;
    }
    case ::TranslationConfig::TranslationBackend::Google:
    {
        out.base_url.clear();
        out.model.clear();
        out.api_key = safe_key(cfg_ui.google_api_key);
        break;
    }
    case ::TranslationConfig::TranslationBackend::ZhipuGLM:
    {
        std::string base = safe_str(cfg_ui.zhipu_base_url);
        if (!base.empty())
        {
            while (!base.empty() && base.back() == '/')
                base.pop_back();
            if (base.find("/chat/completions") == std::string::npos)
            {
                base += "/api/paas/v4/chat/completions";
            }
        }
        else
        {
            base = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
        }
        out.base_url = std::move(base);
        out.model = safe_model(cfg_ui.zhipu_model);
        if (out.model.empty())
            out.model = "glm-4-flash";
        out.api_key = safe_key(cfg_ui.zhipu_api_key);
        break;
    }
    case ::TranslationConfig::TranslationBackend::QwenMT:
    {
        out.base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
        out.model = safe_model(cfg_ui.qwen_model);
        if (out.model.empty())
            out.model = "qwen-mt-turbo";
        out.api_key = safe_key(cfg_ui.qwen_api_key);
        break;
    }
    case ::TranslationConfig::TranslationBackend::Niutrans:
    {
        out.base_url = "https://api.niutrans.com/NiuTransServer/translation";
        out.model.clear();
        out.api_key = safe_key(cfg_ui.niutrans_api_key);
        out.api_secret.clear();
        break;
    }
    case ::TranslationConfig::TranslationBackend::Youdao:
    {
        if (cfg_ui.youdao_mode == ::TranslationConfig::YoudaoMode::LargeModel)
        {
            out.base_url = "https://openapi.youdao.com/llm_trans";
            out.model = "youdao_large";
        }
        else
        {
            out.base_url = "https://openapi.youdao.com/api";
            out.model = "youdao_text";
        }
        out.api_key = safe_key(cfg_ui.youdao_app_key);
        out.api_secret = safe_key(cfg_ui.youdao_app_secret);
        break;
    }
    }

    return out;
}

} // namespace translate
