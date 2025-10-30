#pragma once

#include "ILLMTranslator.hpp"
#include <atomic>
#include <array>

namespace translate
{

class FreeLLMTranslator : public ILLMTranslator
{
public:
    FreeLLMTranslator() = default;
    ~FreeLLMTranslator() override = default;

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
    std::string testConnectionImpl() override;

private:
    // Hardcoded configuration
    static constexpr const char* BASE_URL = "https://wanqing.streamlakeapi.com/api/gateway/v1/endpoints/chat/completions";
    static constexpr std::size_t API_KEY_COUNT = 6;
    // API_KEYS now loaded from generated ApiKeys.h (see ApiKeys.h.in template)

    // Round-robin key rotation (thread-safe atomic counter)
    static std::atomic<std::size_t> key_rotation_index_;

    // Get next API key using round-robin
    std::string getNextApiKey() const;
};

} // namespace translate
