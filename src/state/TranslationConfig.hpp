#pragma once

#include <array>
#include <cstdio>
#include <cstring>

struct TranslationConfig
{
    static constexpr std::size_t LangSize = 32;
    static constexpr std::size_t URLSize = 256;
    static constexpr std::size_t ModelSize = 128;
    static constexpr std::size_t ApiKeySize = 256;

    enum class TargetLang
    {
        EN_US = 0,
        ZH_CN = 1,
        ZH_TW = 2
    };

    enum class TranslationBackend
    {
        OpenAI = 0,
        Google = 1,
        ZhipuGLM = 2,
        QwenMT = 3,
        Niutrans = 4,
        Youdao = 5
    };

    enum class YoudaoMode
    {
        Text = 0,
        LargeModel = 1
    };

    bool translate_enabled;
    bool auto_apply_changes;
    TranslationBackend translation_backend;
    TargetLang target_lang_enum;
    std::array<char, URLSize>   openai_base_url{};
    std::array<char, ModelSize> openai_model{};
    std::array<char, ApiKeySize> openai_api_key{};
    std::array<char, ApiKeySize> google_api_key{};
    // Zhipu (BigModel) GLM settings
    std::array<char, URLSize>   zhipu_base_url{};
    std::array<char, ModelSize> zhipu_model{};
    std::array<char, ApiKeySize> zhipu_api_key{};

    // Qwen-MT (Aliyun) settings
    std::array<char, ModelSize> qwen_model{};
    std::array<char, ApiKeySize> qwen_api_key{};

    // Niutrans settings
    std::array<char, ApiKeySize> niutrans_api_key{};

    // Youdao settings
    std::array<char, ApiKeySize> youdao_app_key{};
    std::array<char, ApiKeySize> youdao_app_secret{};
    YoudaoMode youdao_mode = YoudaoMode::Text;

    void applyDefaults()
    {
        translate_enabled = false;
        auto_apply_changes = true;
        translation_backend = TranslationBackend::OpenAI;
        target_lang_enum = TargetLang::EN_US;
        
        openai_base_url.fill('\0');
        std::snprintf(openai_base_url.data(), openai_base_url.size(), "%s", "https://api.openai.com");
        
        openai_model.fill('\0');
        openai_api_key.fill('\0');
        google_api_key.fill('\0');

        zhipu_base_url.fill('\0');
        // Default to BigModel domain; translator will append path if needed
        std::snprintf(zhipu_base_url.data(), zhipu_base_url.size(), "%s", "https://open.bigmodel.cn");
        zhipu_model.fill('\0');
        zhipu_api_key.fill('\0');

        qwen_model.fill('\0');
        std::snprintf(qwen_model.data(), qwen_model.size(), "%s", "qwen-mt-turbo");
        qwen_api_key.fill('\0');

        niutrans_api_key.fill('\0');

        youdao_app_key.fill('\0');
        youdao_app_secret.fill('\0');
        youdao_mode = YoudaoMode::Text;
    }

    void copyFrom(const TranslationConfig& other)
    {
        translate_enabled = other.translate_enabled;
        auto_apply_changes = other.auto_apply_changes;
        translation_backend = other.translation_backend;
        target_lang_enum = other.target_lang_enum;
        openai_base_url = other.openai_base_url;
        openai_model = other.openai_model;
        openai_api_key = other.openai_api_key;
        google_api_key = other.google_api_key;
        zhipu_base_url = other.zhipu_base_url;
        zhipu_model = other.zhipu_model;
        zhipu_api_key = other.zhipu_api_key;
        qwen_model = other.qwen_model;
        qwen_api_key = other.qwen_api_key;
        niutrans_api_key = other.niutrans_api_key;
        youdao_app_key = other.youdao_app_key;
        youdao_app_secret = other.youdao_app_secret;
        youdao_mode = other.youdao_mode;
    }
};
