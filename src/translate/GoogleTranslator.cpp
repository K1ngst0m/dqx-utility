#include "GoogleTranslator.hpp"

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

GoogleTranslator::GoogleTranslator() = default;

GoogleTranslator::~GoogleTranslator() { shutdown(); }

bool GoogleTranslator::init(const BackendConfig& cfg)
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
    paid_api_working_.store(true);
    warned_about_fallback_ = false;
    running_.store(true);
    worker_ = std::thread(&GoogleTranslator::workerLoop, this);
    return true;
}

bool GoogleTranslator::isReady() const { return running_.load(); }

void GoogleTranslator::shutdown()
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

bool GoogleTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                                 std::uint64_t& out_id)
{
    if (!isReady())
    {
        last_error_ = "translator not ready";
        return false;
    }
    // ignore empty or whitespace-only input
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

bool GoogleTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void GoogleTranslator::workerLoop()
{
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

        const auto interval = std::chrono::duration<double>(request_interval_seconds_);
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

            if (doRequest(j.text, j.src, j.dst, out))
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
            PLOG_INFO << "Translation [" << j.src << " -> " << j.dst << "]: '" << j.text << "' -> '" << out << "'";
            Completed c;
            c.id = j.id;
            c.text = std::move(out);
            c.failed = false;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
        else
        {
            PLOG_WARNING << "Translation failed [" << j.src << " -> " << j.dst << "]: '" << j.text << "' - "
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

bool GoogleTranslator::doRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                                 std::string& out_text)
{
    if (text.empty())
        return false;

    // Try paid API first if API key is available and hasn't failed recently
    if (!cfg_.api_key.empty() && paid_api_working_.load())
    {
        if (tryPaidAPI(text, src_lang, dst_lang, out_text))
        {
            return true;
        }
        else
        {
            paid_api_working_.store(false);
            if (!warned_about_fallback_)
            {
                PLOG_WARNING << "Google Translate paid API failed, falling back to free tier";
                warned_about_fallback_ = true;
            }
        }
    }

    // Fall back to free API
    return tryFreeAPI(text, src_lang, dst_lang, out_text);
}

bool GoogleTranslator::tryPaidAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                                  std::string& out_text)
{
    using namespace translate::helpers;

    // PHASE 1: Text length validation
    auto length_check = check_text_length(text, LengthLimits::GOOGLE_PAID_API_MAX, "Google Paid API");
    if (!length_check.ok)
    {
        last_error_ = length_check.error_message;
        PLOG_WARNING << "Google Paid API text length check failed: " << length_check.error_message;
        PLOG_DEBUG << "Text stats - Bytes: " << length_check.byte_size;
        return false;
    }

    PLOG_DEBUG << "Google Paid API translation request - Text length: " << length_check.byte_size << " bytes";

    std::string url = "https://translation.googleapis.com/language/translate/v2";
    std::string src = normalizeLanguageCode(src_lang);
    std::string dst = normalizeLanguageCode(dst_lang);

    std::string escaped_text = text;
    std::size_t pos = 0;
    while ((pos = escaped_text.find('\"', pos)) != std::string::npos)
    {
        escaped_text.replace(pos, 1, "\\\"");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\n', pos)) != std::string::npos)
    {
        escaped_text.replace(pos, 1, "\\n");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\r', pos)) != std::string::npos)
    {
        escaped_text.replace(pos, 1, "\\r");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\\', pos)) != std::string::npos)
    {
        escaped_text.replace(pos, 1, "\\\\");
        pos += 2;
    }

    std::string body =
        R"({"q": ")" + escaped_text + R"(", "source": ")" + src + R"(", "target": ")" + dst + R"(", "format": "text"})";

    PLOG_DEBUG << "Google Paid API request body size: " << body.size() << " bytes";

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
            PLOG_WARNING << "Google Translate paid API request failed: " << err_msg;
            PLOG_DEBUG << "Original error: " << r.error;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate paid API request failed", err_msg);
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
            PLOG_WARNING << "Google Translate paid API failed: " << err_msg;
            PLOG_DEBUG << "Response body: " << r.text;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate paid API HTTP error", err_msg);
        }
        return false;
    }

    std::string content = extractTranslationFromJSON(r.text);
    if (content.empty())
    {
        std::string err_msg = "parse error";
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate paid API response parse failed: " << r.text;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate paid API response parse failed", r.text);
        }
        return false;
    }
    out_text = std::move(content);
    return true;
}

bool GoogleTranslator::tryFreeAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
                                  std::string& out_text)
{
    using namespace translate::helpers;

    // CRITICAL FIX: Google Free API uses GET with URL encoding
    // This has strict length limits due to URL length restrictions
    auto length_check = check_text_length(text, LengthLimits::GOOGLE_FREE_API_MAX, "Google Free API");
    if (!length_check.ok)
    {
        last_error_ =
            length_check.error_message + " (Google Free API uses URL encoding - try paid API for longer texts)";
        PLOG_WARNING << "Google Free API rejected due to text length: " << length_check.byte_size
                     << " bytes (limit: " << LengthLimits::GOOGLE_FREE_API_MAX << ")";
        return false;
    }

    PLOG_DEBUG << "Google Free API request - Text length: " << length_check.byte_size << " bytes";

    std::string src = normalizeLanguageCode(src_lang);
    std::string dst = normalizeLanguageCode(dst_lang);

    std::string url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=" + escapeUrl(src) +
                      "&tl=" + escapeUrl(dst) + "&dt=t&q=" + escapeUrl(text);

    // Diagnostic: log URL length
    PLOG_DEBUG << "Google Free API URL length: " << url.size() << " bytes";

    // PHASE 2: Adaptive timeout
    translate::SessionConfig scfg;
    scfg.connect_timeout_ms = 5000;
    scfg.timeout_ms = 30000;
    scfg.text_length_hint = text.size();
    scfg.use_adaptive_timeout = true;
    scfg.cancel_flag = &running_;

    auto r = translate::get(url, {}, scfg);

    // PHASE 1: Enhanced error handling
    if (!r.error.empty())
    {
        auto err_type = categorize_http_error(0, r.error);
        std::string err_msg = get_error_description(err_type, 0, r.error);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate free API request failed: " << err_msg;
            PLOG_DEBUG << "Original error: " << r.error;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate free API request failed", err_msg);
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
            PLOG_WARNING << "Google Translate free API failed: " << err_msg;
            PLOG_DEBUG << "Response body: " << r.text;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate free API HTTP error", err_msg);
        }
        return false;
    }

    std::string content = extractTranslationFromFreeAPI(r.text);
    if (content.empty())
    {
        std::string err_msg = "parse error";
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate free API response parse failed: " << r.text;
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                                "Google Translate free API response parse failed", r.text);
        }
        return false;
    }
    out_text = std::move(content);
    return true;
}

std::string GoogleTranslator::escapeUrl(const std::string& s)
{
    std::string escaped;
    escaped.reserve(s.size() * 3);

    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped += c;
        }
        else
        {
            escaped += '%';
            static const char hex[] = "0123456789ABCDEF";
            escaped += hex[c >> 4];
            escaped += hex[c & 0x0F];
        }
    }
    return escaped;
}

std::string GoogleTranslator::extractTranslationFromJSON(const std::string& body)
{
    const std::string key = "\"translatedText\"";
    size_t start = body.find(key);
    if (start == std::string::npos)
        return "";

    start = body.find(':', start);
    if (start == std::string::npos)
        return "";
    ++start;

    while (start < body.size() && std::isspace(body[start]))
        ++start;

    if (start >= body.size() || body[start] != '\"')
        return "";
    ++start;

    size_t end = findQuoteEnd(body, start);
    if (end == std::string::npos)
        return "";

    return unescapeJSONString(body.substr(start, end - start));
}

std::string GoogleTranslator::extractTranslationFromFreeAPI(const std::string& body)
{
    const std::string pattern = "[[[\"";
    size_t start = body.find(pattern);
    if (start == std::string::npos)
        return "";

    start += pattern.length();

    size_t end = findQuoteEnd(body, start);
    if (end == std::string::npos)
        return "";

    return unescapeJSONString(body.substr(start, end - start));
}

size_t GoogleTranslator::findQuoteEnd(const std::string& body, size_t start)
{
    size_t pos = start;
    bool escaped = false;

    while (pos < body.size())
    {
        if (escaped)
        {
            escaped = false;
            ++pos;
            continue;
        }

        char c = body[pos];
        if (c == '\\')
        {
            escaped = true;
            ++pos;
        }
        else if (c == '\"')
        {
            return pos;
        }
        else
        {
            ++pos;
        }
    }

    return std::string::npos;
}

std::string GoogleTranslator::unescapeJSONString(const std::string& escaped)
{
    std::string unescaped;
    for (size_t i = 0; i < escaped.size(); ++i)
    {
        if (escaped[i] == '\\' && i + 1 < escaped.size())
        {
            char next = escaped[i + 1];
            if (next == '\"')
            {
                unescaped += '\"';
                ++i;
            }
            else if (next == '\\')
            {
                unescaped += '\\';
                ++i;
            }
            else if (next == 'n')
            {
                unescaped += '\n';
                ++i;
            }
            else if (next == 'r')
            {
                unescaped += '\r';
                ++i;
            }
            else if (next == 't')
            {
                unescaped += '\t';
                ++i;
            }
            else
                unescaped += escaped[i];
        }
        else
        {
            unescaped += escaped[i];
        }
    }
    return unescaped;
}

std::string GoogleTranslator::normalizeLanguageCode(const std::string& lang_code)
{
    if (lang_code == "en-us")
        return "en";
    if (lang_code == "zh-cn")
        return "zh-cn";
    if (lang_code == "zh-tw")
        return "zh-tw";
    if (lang_code == "ja-jp")
        return "ja";
    if (lang_code == "ko-kr")
        return "ko";
    return lang_code;
}

std::string GoogleTranslator::testConnection()
{
    std::string test_text = "Hello";
    std::string target_lang = cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang;
    std::string result;

    if (!cfg_.api_key.empty())
    {
        if (tryPaidAPI(test_text, "en", target_lang, result))
        {
            if (result.empty())
                return "Error: Paid API returned empty result";
            return "Success: Google Translate paid API connection test passed";
        }
        else
        {
            if (tryFreeAPI(test_text, "en", target_lang, result))
            {
                if (result.empty())
                    return "Warning: Paid API failed, free API returned empty result";
                return "Warning: Paid API failed (check API key), falling back to free tier";
            }
            else
            {
                return "Error: Both paid and free APIs failed - " + last_error_;
            }
        }
    }
    else
    {
        if (tryFreeAPI(test_text, "en", target_lang, result))
        {
            if (result.empty())
                return "Error: Free API returned empty result";
            return "Success: Google Translate free API connection test passed";
        }
        else
        {
            return "Error: Free API test failed - " + last_error_;
        }
    }
}
