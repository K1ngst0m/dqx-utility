#pragma once

#include "ITranslator.hpp"

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>

namespace translate
{

class YoudaoTranslator : public ITranslator
{
public:
    YoudaoTranslator();
    ~YoudaoTranslator() override;

    bool init(const BackendConfig& cfg) override;
    bool isReady() const override;
    void shutdown() override;
    bool translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                   std::uint64_t& out_id) override;
    bool drain(std::vector<Completed>& out) override;

    const char* lastError() const override { return last_error_.c_str(); }

    std::string testConnection() override;

private:
    struct Job
    {
        std::uint64_t id = 0;
        std::string text;
        std::string src;
        std::string dst;
    };
    enum class Mode
    {
        Text,
        LargeModel
    };

    void workerLoop();
    bool doTextRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                       std::string& out_text);
    bool doLargeModelRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                             std::string& out_text);
    bool parseTextResponse(const std::string& body, std::string& out_text);
    bool parseLargeModelResponse(const std::string& body, std::string& out_text);

    static std::string mapSource(const std::string& lang, Mode mode);
    static std::string mapTarget(const std::string& lang, Mode mode);
    static std::string truncateInput(const std::string& text);
    static std::string makeSalt();
    static std::string makeCurtime();
    static std::string buildSignature(const std::string& app_key, const std::string& app_secret,
                                      const std::string& input, const std::string& salt, const std::string& curtime);
    static std::string sha256Hex(const std::string& data);
    static std::string trimCopy(const std::string& value);
    static std::string unescapeJson(const std::string& value);
    static bool extractJsonString(const std::string& json, const std::string& key, std::string& out_value);

#ifdef DQX_UTILITY_ENABLE_TEST_HOOKS
public:
    static std::string debugSha256(const std::string& data) { return sha256Hex(data); }

    static std::string debugBuildSignature(const std::string& app_key, const std::string& app_secret,
                                           const std::string& input, const std::string& salt,
                                           const std::string& curtime)
    {
        return buildSignature(app_key, app_secret, input, salt, curtime);
    }

    static std::string debugTruncateInput(const std::string& text) { return truncateInput(text); }
#endif

    BackendConfig cfg_{};
    Mode mode_ = Mode::Text;
    std::atomic<bool> running_{ false };
    std::thread worker_;
    std::atomic<std::uint64_t> next_id_{ 1 };
    std::string last_error_;
    std::once_flag trad_warn_once_;

    std::mutex q_mtx_;
    std::queue<Job> queue_;

    std::mutex r_mtx_;
    std::vector<Completed> results_;

    std::size_t max_concurrent_requests_ = 3;
    double request_interval_seconds_ = 0.5;
    int max_retries_ = 3;
    std::atomic<std::size_t> in_flight_{ 0 };
    std::chrono::steady_clock::time_point last_request_;
    std::mutex rate_mtx_;
};

} // namespace translate
