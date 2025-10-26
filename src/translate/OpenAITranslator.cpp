#include "OpenAITranslator.hpp"

#include <nlohmann/json.hpp>
#include <plog/Log.h>

namespace translate
{

const char* OpenAITranslator::providerName() const
{
    return "OpenAI";
}

std::string OpenAITranslator::validateConfig(const BackendConfig& cfg) const
{
    if (cfg.api_key.empty())
        return "Missing API key";
    if (cfg.base_url.empty())
        return "Missing base URL";
    if (cfg.model.empty())
        return "Missing model";
    return {};
}

bool OpenAITranslator::hasValidRuntimeConfig() const
{
    return !cfg_.api_key.empty() && !cfg_.model.empty() && !cfg_.base_url.empty();
}

ILLMTranslator::ProviderLimits OpenAITranslator::providerLimits() const
{
    ProviderLimits limits;
    limits.max_input_bytes = helpers::LengthLimits::OPENAI_API_MAX;
    return limits;
}

void OpenAITranslator::buildHeaders(const Job& job, std::vector<Header>& headers) const
{
    (void)job;
    headers.push_back({ "Content-Type", "application/json" });
    headers.push_back({ "Authorization", std::string("Bearer ") + cfg_.api_key });
}

std::string OpenAITranslator::buildUrl(const Job& job) const
{
    (void)job;
    return normalizeURL(cfg_.base_url);
}

void OpenAITranslator::buildRequestBody(const Job& job, const Prompt& prompt, nlohmann::json& body) const
{
    (void)job;
    body["model"] = cfg_.model;
    nlohmann::json messages = nlohmann::json::array();
    for (const auto& message : prompt.messages)
    {
        std::string role = "user";
        switch (message.role)
        {
        case Role::System:
            role = "system";
            break;
        case Role::Assistant:
            role = "assistant";
            break;
        default:
            role = "user";
            break;
        }
        messages.push_back({ { "role", role }, { "content", message.content } });
    }
    body["messages"] = std::move(messages);
    body["temperature"] = 0.3;
}

ILLMTranslator::ParseResult OpenAITranslator::parseResponse(const Job& job, const HttpResponse& resp,
                                                            Completed& out) const
{
    (void)job;
    ParseResult result;
    try
    {
        auto json = nlohmann::json::parse(resp.text);
        if (!json.contains("choices") || json["choices"].empty())
        {
            result.error_message = "missing choices in response";
            return result;
        }

        const auto& choice = json["choices"].at(0);
        if (!choice.contains("message") || !choice["message"].contains("content"))
        {
            result.error_message = "missing message content";
            return result;
        }

        out.text = choice["message"]["content"].get<std::string>();
        result.ok = true;
        return result;
    }
    catch (const std::exception& ex)
    {
        result.error_message = std::string("parse error: ") + ex.what();
        return result;
    }
}

std::string OpenAITranslator::connectionSuccessMessage() const
{
    return "Success: Connection test passed, model responded correctly";
}

std::string OpenAITranslator::testConnectionImpl()
{
    if (cfg_.api_key.empty())
        return "Config Error: Missing API key";
    if (cfg_.base_url.empty())
        return "Config Error: Missing base URL";
    if (cfg_.model.empty())
        return "Config Error: Missing model";

    std::string models_url = cfg_.base_url;
    if (!models_url.empty() && models_url.back() == '/')
        models_url.pop_back();
    models_url += "/v1/models";

    std::vector<Header> headers{ { "Authorization", std::string("Bearer ") + cfg_.api_key } };
    SessionConfig session;
    session.connect_timeout_ms = 3000;
    session.timeout_ms = 8000;
    session.cancel_flag = &running_;

    const auto resp = translate::get(models_url, headers, session);
    if (!resp.error.empty())
        return "Error: Cannot connect to base URL - " + resp.error;
    if (resp.status_code < 200 || resp.status_code >= 300)
        return "Error: Base URL returned HTTP " + std::to_string(resp.status_code);

    if (resp.text.find('"' + cfg_.model + '"') == std::string::npos)
        return "Warning: Model '" + cfg_.model + "' not found in available models list";

    return ILLMTranslator::testConnectionImpl();
}

std::string OpenAITranslator::normalizeURL(const std::string& base_url)
{
    std::string url = base_url;

    while (!url.empty() && url.back() == '/')
        url.pop_back();

    if (url.empty())
        return url;

    size_t scheme_end = url.find("://");
    size_t path_start = (scheme_end != std::string::npos) ? url.find('/', scheme_end + 3) : url.find('/');

    if (path_start != std::string::npos)
    {
        std::string path = url.substr(path_start);
        if (path.find("/v1/chat/completions") != std::string::npos)
            return url;
        if (path == "/v1")
            return url + "/chat/completions";
        return url;
    }

    return url + "/v1/chat/completions";
}

} // namespace translate
