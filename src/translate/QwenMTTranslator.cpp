#include "QwenMTTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>
#include <chrono>
#include "../utils/HttpCommon.hpp"
#include "../utils/ErrorReporter.hpp"
#include "TranslatorHelpers.hpp"

namespace
{
struct FlightGuard
{
    std::atomic<std::size_t>& ref;

    explicit FlightGuard(std::atomic<std::size_t>& r)
        : ref(r)
    {
    }

    ~FlightGuard() { ref.fetch_sub(1, std::memory_order_relaxed); }
};
} // namespace

using namespace translate;

QwenMTTranslator::QwenMTTranslator() = default;

QwenMTTranslator::~QwenMTTranslator() { shutdown(); }

bool QwenMTTranslator::init(const BackendConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
    max_concurrent_requests_ = cfg_.max_concurrent_requests == 0 ? 1 : cfg_.max_concurrent_requests;
    request_interval_seconds_ = cfg_.request_interval_seconds < 0.0 ? 0.0 : cfg_.request_interval_seconds;
    max_retries_ = cfg_.max_retries < 0 ? 0 : cfg_.max_retries;
    in_flight_.store(0, std::memory_order_relaxed);
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    last_request_ =
        std::chrono::steady_clock::now() - std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
    running_.store(true);
    worker_ = std::thread(&QwenMTTranslator::workerLoop, this);
    return true;
}

bool QwenMTTranslator::isReady() const
{
    return running_.load() && !cfg_.model.empty() && !cfg_.base_url.empty() && !cfg_.api_key.empty();
}

void QwenMTTranslator::shutdown()
{
    running_.store(false);
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lk(q_mtx_);
        std::queue<Job> empty;
        std::swap(queue_, empty);
    }
    {
        std::lock_guard<std::mutex> lk(r_mtx_);
        results_.clear();
    }
    in_flight_.store(0, std::memory_order_relaxed);
}

bool QwenMTTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                                 std::uint64_t& out_id)
{
    if (!isReady())
    {
        last_error_ = "translator not ready";
        return false;
    }
    bool all_space = true;
    for (char c : text)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            all_space = false;
            break;
        }
    }
    if (text.empty() || all_space)
        return false;
    Job j;
    j.id = next_id_++;
    j.text = text;
    j.src = src_lang;
    j.dst = dst_lang;
    {
        std::lock_guard<std::mutex> lk(q_mtx_);
        queue_.push(std::move(j));
    }
    out_id = j.id;
    return true;
}

bool QwenMTTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void QwenMTTranslator::workerLoop()
{
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    while (running_.load())
    {
        Job j;
        {
            std::lock_guard<std::mutex> lk(q_mtx_);
            if (!queue_.empty())
            {
                j = std::move(queue_.front());
                queue_.pop();
            }
        }
        if (j.id == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        in_flight_.fetch_add(1, std::memory_order_relaxed);
        FlightGuard guard(in_flight_);

        bool success = false;
        std::string out;
        int attempt = 0;
        while (running_.load())
        {
            if (interval.count() > 0.0)
            {
                std::chrono::steady_clock::time_point wait_until;
                {
                    std::lock_guard<std::mutex> lock(rate_mtx_);
                    wait_until =
                        last_request_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
                }
                auto now = std::chrono::steady_clock::now();
                if (wait_until > now)
                {
                    std::this_thread::sleep_for(wait_until - now);
                    if (!running_.load())
                        break;
                }
            }

            if (doRequest(j.text, j.dst, out))
            {
                success = true;
                {
                    std::lock_guard<std::mutex> lock(rate_mtx_);
                    last_request_ = std::chrono::steady_clock::now();
                }
                break;
            }

            {
                std::lock_guard<std::mutex> lock(rate_mtx_);
                last_request_ = std::chrono::steady_clock::now();
            }

            if (attempt >= max_retries_)
            {
                break;
            }
            ++attempt;
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
        }

        if (success)
        {
            PLOG_INFO << "Qwen-MT Translation [" << j.src << " -> " << j.dst << "]: '" << j.text << "' -> '" << out
                      << "'";
            Completed c;
            c.id = j.id;
            c.text = std::move(out);
            c.failed = false;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
        else
        {
            PLOG_WARNING << "Qwen-MT Translation failed [" << j.src << " -> " << j.dst << "]: '" << j.text << "' - "
                         << last_error_;
            Completed c;
            c.id = j.id;
            c.failed = true;
            c.original_text = j.text;
            c.error_message = last_error_;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

std::string QwenMTTranslator::escapeJSON(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
        case '\\':
            o += "\\\\";
            break;
        case '"':
            o += "\\\"";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            o += c;
            break;
        }
    }
    return o;
}

std::string QwenMTTranslator::mapTarget(const std::string& dst_lang)
{
    if (dst_lang == "en-us")
        return "English";
    if (dst_lang == "zh-cn")
        return "Chinese";
    if (dst_lang == "zh-tw")
        return "Chinese (Traditional)"; // best-effort label
    return dst_lang;
}

bool QwenMTTranslator::doRequest(const std::string& text, const std::string& dst_lang, std::string& out_text)
{
    using namespace translate::helpers;

    // PHASE 1: Text length validation with diagnostic logging
    auto length_check = check_text_length(text, LengthLimits::QWEN_MT_API_MAX, "Qwen-MT");
    if (!length_check.ok)
    {
        last_error_ = length_check.error_message;
        PLOG_WARNING << "Qwen-MT text length check failed: " << length_check.error_message;
        PLOG_DEBUG << "Text stats - Bytes: " << length_check.byte_size;
        return false;
    }

    PLOG_DEBUG << "Qwen-MT translation request - Text length: " << length_check.byte_size << " bytes";

    if (text.empty())
        return false;

    std::string url = cfg_.base_url;

    std::string target = mapTarget(dst_lang);

    // PHASE 2: Fix buffer reservation
    std::string body;
    body.reserve(calculate_json_buffer_size(text.size()));
    body += "{\"model\":\"" + cfg_.model + "\",";
    body += "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJSON(text) + "\"}],";
    body += "\"translation_options\":{\"source_lang\":\"auto\",\"target_lang\":\"" + target + "\"}}";

    PLOG_DEBUG << "Qwen-MT request body size: " << body.size() << " bytes";

    std::vector<translate::Header> headers{
        { "Content-Type",  "application/json"                    },
        { "Authorization", std::string("Bearer ") + cfg_.api_key }
    };

    // PHASE 2: Adaptive timeout
    translate::SessionConfig scfg;
    scfg.connect_timeout_ms = 5000;
    scfg.timeout_ms = 45000;
    scfg.text_length_hint = text.size();
    scfg.use_adaptive_timeout = true;
    scfg.cancel_flag = &running_;

    auto r = translate::post_json(url, body, headers, scfg);

    // PHASE 1: Enhanced error handling
    if (!r.error.empty())
    {
        auto err_type = categorize_http_error(0, r.error);
        std::string err_msg = get_error_description(err_type, 0, r.error);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Qwen-MT request failed: " << err_msg;
            PLOG_DEBUG << "Original error: " << r.error;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation, "Qwen-MT request failed", err_msg);
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        auto err_type = categorize_http_error(r.status_code, "");
        std::string err_msg = get_error_description(err_type, r.status_code, r.text);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Qwen-MT request failed: " << err_msg;
            PLOG_DEBUG << "Response body: " << r.text;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation, "Qwen-MT HTTP error",
                                                std::to_string(r.status_code) + ": " + r.text);
        }
        return false;
    }

    const std::string key = "\"content\"";
    size_t p = r.text.find(key);
    if (p == std::string::npos)
        return false;
    p = r.text.find(':', p);
    if (p == std::string::npos)
        return false;
    ++p;
    while (p < r.text.size() && (r.text[p] == ' ' || r.text[p] == '\t' || r.text[p] == '\n' || r.text[p] == '\r'))
        ++p;
    if (p >= r.text.size() || r.text[p] != '"')
        return false;
    ++p;
    std::string v;
    while (p < r.text.size())
    {
        char c = r.text[p++];
        if (c == '\\')
        {
            if (p >= r.text.size())
                break;
            char e = r.text[p++];
            if (e == 'n')
                v.push_back('\n');
            else if (e == 'r')
                v.push_back('\r');
            else if (e == 't')
                v.push_back('\t');
            else
                v.push_back(e);
        }
        else if (c == '"')
        {
            break;
        }
        else
        {
            v.push_back(c);
        }
    }
    out_text.swap(v);
    return true;
}

std::string QwenMTTranslator::testConnection()
{
    if (cfg_.api_key.empty())
        return "Config Error: Missing API key";
    if (cfg_.base_url.empty())
        return "Config Error: Missing base URL";
    if (cfg_.model.empty())
        return "Config Error: Missing model";

    std::string result;
    if (!doRequest("Hello", cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang, result))
    {
        if (last_error_.empty())
            return "Error: Test translation failed";
        return std::string("Error: Test translation failed - ") + last_error_;
    }
    if (result.empty())
        return "Error: Test translation returned empty result";
    return "Success: Qwen-MT connection test passed";
}
