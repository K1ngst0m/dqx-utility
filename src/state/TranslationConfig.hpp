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
        Google = 1
    };

    bool translate_enabled;
    bool auto_apply_changes;
    TranslationBackend translation_backend;
    TargetLang target_lang_enum;
    std::array<char, URLSize>   openai_base_url{};
    std::array<char, ModelSize> openai_model{};
    std::array<char, ApiKeySize> openai_api_key{};
    std::array<char, ApiKeySize> google_api_key{};

    void applyDefaults()
    {
        translate_enabled = false;
        auto_apply_changes = false;
        translation_backend = TranslationBackend::OpenAI;
        target_lang_enum = TargetLang::EN_US;
        
        openai_base_url.fill('\0');
        std::snprintf(openai_base_url.data(), openai_base_url.size(), "%s", "https://api.openai.com");
        
        openai_model.fill('\0');
        openai_api_key.fill('\0');
        google_api_key.fill('\0');
    }
};
