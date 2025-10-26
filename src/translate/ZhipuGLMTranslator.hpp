#pragma once

#include "ILLMTranslator.hpp"

namespace translate
{

class ZhipuGLMTranslator : public ILLMTranslator
{
public:
    ZhipuGLMTranslator() = default;
    ~ZhipuGLMTranslator() override = default;

protected:
    const char* providerName() const override;
    std::string validateConfig(const BackendConfig& cfg) const override;
    bool hasValidRuntimeConfig() const override;
    ProviderLimits providerLimits() const override;
    void buildHeaders(const Job& job, std::vector<Header>& headers) const override;
    std::string buildUrl(const Job& job) const override;
    void buildRequestBody(const Job& job, const Prompt& prompt, nlohmann::json& body) const override;
    ParseResult parseResponse(const Job& job, const HttpResponse& resp, Completed& out) const override;
    std::string connectionSuccessMessage() const override;
};

} // namespace translate
