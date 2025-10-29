#include "ZhipuGLMTranslator.hpp"

#include <nlohmann/json.hpp>

namespace translate
{

const char* ZhipuGLMTranslator::providerName() const { return "ZhipuGLM"; }

std::string ZhipuGLMTranslator::validateConfig(const BackendConfig& cfg) const
{
    if (cfg.api_key.empty())
        return "Missing API key";
    if (cfg.base_url.empty())
        return "Missing base URL";
    if (cfg.model.empty())
        return "Missing model";
    return {};
}

bool ZhipuGLMTranslator::hasValidRuntimeConfig() const
{
    return !cfg_.api_key.empty() && !cfg_.model.empty() && !cfg_.base_url.empty();
}

ILLMTranslator::ProviderLimits ZhipuGLMTranslator::providerLimits() const
{
    ProviderLimits limits;
    limits.max_input_bytes = helpers::LengthLimits::ZHIPU_GLM_API_MAX;
    return limits;
}

void ZhipuGLMTranslator::buildHeaders(const Job& job, std::vector<Header>& headers) const
{
    (void)job;
    headers.push_back({ "Content-Type", "application/json" });
    if (!cfg_.api_key.empty())
        headers.push_back({ "Authorization", std::string("Bearer ") + cfg_.api_key });
}

std::string ZhipuGLMTranslator::buildUrl(const Job& job) const
{
    (void)job;
    return cfg_.base_url;
}

void ZhipuGLMTranslator::buildRequestBody(const Job& job, const Prompt& prompt, nlohmann::json& body) const
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
        messages.push_back({
            { "role",    role            },
            { "content", message.content }
        });
    }
    body["messages"] = std::move(messages);
    body["temperature"] = 0.3;
    body["top_p"] = 0.7;
    body["stream"] = false;
}

ILLMTranslator::ParseResult ZhipuGLMTranslator::parseResponse(const Job& job, const HttpResponse& resp,
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

std::string ZhipuGLMTranslator::connectionSuccessMessage() const
{
    return "Success: GLM-4 Flash connection test passed";
}

} // namespace translate
