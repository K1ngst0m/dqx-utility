#pragma once

#include "ITranslator.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>

namespace translate
{
    // Google Translate service with both free and paid API support
    // Automatically falls back to free tier when API key is invalid/expired
    // Uses worker thread to handle HTTP requests without blocking UI
    class GoogleTranslator : public ITranslator
    {
    public:
        GoogleTranslator();
        ~GoogleTranslator() override;

        bool init(const BackendConfig& cfg) override;
        bool isReady() const override;
        void shutdown() override;
        bool translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id) override;
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

        void workerLoop();
        bool doRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text);
        bool tryPaidAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text);
        bool tryFreeAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text);
        
        static std::string escapeUrl(const std::string& s);
        static std::string extractTranslationFromJSON(const std::string& body);
        static std::string extractTranslationFromFreeAPI(const std::string& body);
        static std::string normalizeLanguageCode(const std::string& lang_code);
        static std::string unescapeJSONString(const std::string& escaped);
        static size_t findQuoteEnd(const std::string& body, size_t start_pos);

        BackendConfig cfg_{};
        std::atomic<bool> running_{false};
        std::thread worker_;
        std::mutex q_mtx_;
        std::queue<Job> queue_;
        std::mutex r_mtx_;
        std::vector<Completed> results_;
        std::uint64_t next_id_ = 1;
        std::string last_error_;
        
        // Track API status
        std::atomic<bool> paid_api_working_{true};
        bool warned_about_fallback_ = false;
    };
}
