#pragma once

#include "ITranslator.hpp"

#include "TranslatorHelpers.hpp"
#include "../processing/GlossaryManager.hpp"
#include "../utils/ErrorReporter.hpp"
#include "../utils/HttpCommon.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace translate
{

class ILLMTranslator : public ITranslator
{
public:
    ILLMTranslator();
    ~ILLMTranslator() override;

    bool init(const BackendConfig& cfg) override;
    bool isReady() const override;
    void shutdown() override;
    bool translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                   std::uint64_t& out_id) override;
    bool drain(std::vector<Completed>& out) override;

    const char* lastError() const override { return last_error_.c_str(); }

    std::string testConnection() override;

protected:
    struct Job
    {
        std::uint64_t id = 0;
        std::string text;
        std::string src;
        std::string dst;
    };

    enum class Role
    {
        System,
        User,
        Assistant
    };

    struct ChatMessage
    {
        Role role = Role::User;
        std::string content;
    };

    struct Prompt
    {
        std::vector<ChatMessage> messages;
    };

    struct PromptContext
    {
        std::string source_lang;
        std::string target_lang;
        std::vector<std::pair<std::string, std::string>> replacements;
    };

    struct ProviderLimits
    {
        std::size_t max_input_bytes = helpers::LengthLimits::OPENAI_API_MAX;
    };

    struct ParseResult
    {
        bool ok = false;
        bool retryable = false;
        double retry_after_seconds = 0.0;
        std::string error_message;
    };

    struct RequestResult
    {
        bool success = false;
        bool retryable = false;
        double retry_after_seconds = 0.0;
        std::string error_message;
        Completed completed;
    };

    virtual const char* providerName() const = 0;
    virtual void onInit();
    virtual std::string validateConfig(const BackendConfig& cfg) const;
    virtual ProviderLimits providerLimits() const;
    virtual bool hasValidRuntimeConfig() const;
    virtual void buildHeaders(const Job& job, std::vector<Header>& headers) const = 0;
    virtual std::string buildUrl(const Job& job) const = 0;
    virtual void buildRequestBody(const Job& job, const Prompt& prompt, nlohmann::json& body) const = 0;
    virtual ParseResult parseResponse(const Job& job, const HttpResponse& resp, Completed& out) const = 0;
    virtual bool shouldRetry(const HttpResponse& resp) const;
    virtual void augmentPromptContext(const Job& job, PromptContext& ctx) const;
    virtual void configureSession(const Job& job, SessionConfig& cfg) const;
    virtual std::string connectionSuccessMessage() const;
    virtual std::string testConnectionImpl();

    Prompt buildPrompt(const Job& job) const;
    static std::string languageDisplayName(const std::string& lang);
    static void replaceAll(std::string& target, const std::string& placeholder, const std::string& value);
    RequestResult performRequest(const Job& job);
    void workerLoop();
    std::string buildGlossarySnippet(const Job& job, const std::string& target_lang) const;
    static processing::GlossaryManager& sharedGlossaryManager();

    struct FlightGuard
    {
        explicit FlightGuard(std::atomic<std::size_t>& counter)
            : ref(counter)
        {
        }

        ~FlightGuard() { ref.fetch_sub(1, std::memory_order_relaxed); }

        std::atomic<std::size_t>& ref;
    };

    BackendConfig cfg_{};
    std::atomic<bool> running_{ false };
    std::thread worker_;
    std::atomic<std::uint64_t> next_id_{ 1 };
    std::string last_error_;

    mutable std::mutex q_mtx_;
    std::queue<Job> queue_;

    mutable std::mutex r_mtx_;
    std::vector<Completed> results_;

    std::size_t max_concurrent_requests_ = 1;
    double request_interval_seconds_ = 0.0;
    int max_retries_ = 0;
    std::atomic<std::size_t> in_flight_{ 0 };
    std::chrono::steady_clock::time_point last_request_{};
    mutable std::mutex rate_mtx_;
};

} // namespace translate
