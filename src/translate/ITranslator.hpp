#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declaration to avoid hard include here
struct TranslationConfig;

namespace translate
{
    enum class Backend
    {
        OpenAI = 0,
        Google = 1,
        ZhipuGLM = 2,
        QwenMT = 3,
        Niutrans = 4,
        Youdao = 5
    };

    struct TranslatorConfig
    {
        Backend backend = Backend::OpenAI;
        std::string target_lang;
        std::string base_url;
        std::string model;
        std::string api_key;
        std::string api_secret;

        static TranslatorConfig from(const ::TranslationConfig& cfg_ui);
    };

    struct Completed
    {
        std::uint64_t id = 0;
        std::string text;
        bool failed = false;
        std::string original_text;
        std::string error_message;
    };

    class ITranslator
    {
    public:
        virtual ~ITranslator() = default;
        virtual bool init(const TranslatorConfig& cfg) = 0;
        virtual bool isReady() const = 0;
        virtual void shutdown() = 0;
        virtual bool translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id) = 0;
        virtual bool drain(std::vector<Completed>& out) = 0;
        virtual const char* lastError() const = 0;
        virtual std::string testConnection() = 0;
    };

    // Factory function to create translators based on backend type
    std::unique_ptr<ITranslator> createTranslator(Backend backend);
}