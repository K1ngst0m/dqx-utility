#pragma once

#include <array>

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

    bool translate_enabled = false;
    TranslationBackend translation_backend = TranslationBackend::OpenAI;
    TargetLang target_lang_enum = TargetLang::EN_US;
    std::array<char, URLSize>   openai_base_url{};
    std::array<char, ModelSize> openai_model{};
    std::array<char, ApiKeySize> openai_api_key{};
    std::array<char, ApiKeySize> google_api_key{};
};