#pragma once

#include "ITranslator.hpp"

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

namespace translate {

class QwenMTTranslator : public ITranslator {
public:
    QwenMTTranslator();
    ~QwenMTTranslator() override;

    bool init(const BackendConfig& cfg) override;
    bool isReady() const override;
    void shutdown() override;
    bool translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id) override;
    bool drain(std::vector<Completed>& out) override;
    const char* lastError() const override { return last_error_.c_str(); }
    std::string testConnection() override;

private:
    struct Job { std::uint64_t id=0; std::string text; std::string src; std::string dst; };

    void workerLoop();
    bool doRequest(const std::string& text, const std::string& dst_lang, std::string& out_text);
    static std::string escapeJSON(const std::string& s);
    static std::string mapTarget(const std::string& dst_lang);

    BackendConfig cfg_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::atomic<std::uint64_t> next_id_{1};
    std::string last_error_;

    std::mutex q_mtx_;
    std::queue<Job> queue_;

    std::mutex r_mtx_;
    std::vector<Completed> results_;
};

} // namespace translate
