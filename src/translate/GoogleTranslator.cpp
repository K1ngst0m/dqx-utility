#include "GoogleTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>
#include "HttpCommon.hpp"

using namespace translate;

GoogleTranslator::GoogleTranslator() = default;
GoogleTranslator::~GoogleTranslator() { shutdown(); }

bool GoogleTranslator::init(const TranslatorConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
    paid_api_working_.store(true);
    warned_about_fallback_ = false;
    running_.store(true);
    worker_ = std::thread(&GoogleTranslator::workerLoop, this);
    return true;
}

bool GoogleTranslator::isReady() const
{
    return running_.load();
}

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
}

bool GoogleTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
{
    if (!isReady())
    {
        last_error_ = "translator not ready";
        return false;
    }
    // ignore empty or whitespace-only input
    bool all_space = true;
    for (char c : text) { if (!std::isspace(static_cast<unsigned char>(c))) { all_space = false; break; } }
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
        std::string out;
        if (doRequest(j.text, j.src, j.dst, out))
        {
            PLOG_INFO << "Translation [" << j.src << " -> " << j.dst << "]: '" << j.text << "' -> '" << out << "'";
            Completed c; c.id = j.id; c.text = std::move(out); c.failed = false;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
        else
        {
            PLOG_WARNING << "Translation failed [" << j.src << " -> " << j.dst << "]: '" << j.text << "' - " << last_error_;
            Completed c; c.id = j.id; c.failed = true; c.original_text = j.text; c.error_message = last_error_;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

bool GoogleTranslator::doRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text)
{
    if (text.empty()) return false;
    
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

bool GoogleTranslator::tryPaidAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text)
{
    std::string url = "https://translation.googleapis.com/language/translate/v2";
    std::string src = normalizeLanguageCode(src_lang);
    std::string dst = normalizeLanguageCode(dst_lang);
    
    std::string escaped_text = text;
    std::size_t pos = 0;
    while ((pos = escaped_text.find('\"', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\\"");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\n', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\n");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\r', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\r");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\\', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\\\");
        pos += 2;
    }
    
    std::string body = R"({"q": ")" + escaped_text + R"(", "source": ")" + src + R"(", "target": ")" + dst + R"(", "format": "text"})";
    
    std::vector<translate::Header> headers{{"Content-Type","application/json"},{"Authorization", std::string("Bearer ")+cfg_.api_key}};
    translate::SessionConfig scfg; scfg.connect_timeout_ms = 5000; scfg.timeout_ms = 45000; scfg.cancel_flag = &running_;
    auto r = translate::post_json(url, body, headers, scfg);
    if (!r.error.empty())
    {
        std::string err_msg = r.error;
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate paid API request failed: " << err_msg;
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(r.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate paid API failed with status " << r.status_code << ": " << r.text;
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
        }
        return false;
    }
    out_text = std::move(content);
    return true;
}

bool GoogleTranslator::tryFreeAPI(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text)
{
    std::string src = normalizeLanguageCode(src_lang);
    std::string dst = normalizeLanguageCode(dst_lang);
    
    std::string url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=" + 
                      escapeUrl(src) + "&tl=" + escapeUrl(dst) + "&dt=t&q=" + escapeUrl(text);
    
    translate::SessionConfig scfg; scfg.connect_timeout_ms = 5000; scfg.timeout_ms = 30000; scfg.cancel_flag = &running_;
    auto r = translate::get(url, {}, scfg);
    if (!r.error.empty())
    {
        std::string err_msg = r.error;
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate free API request failed: " << err_msg;
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(r.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Google Translate free API failed with status " << r.status_code << ": " << r.text;
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
    if (start == std::string::npos) return "";
    
    start = body.find(':', start);
    if (start == std::string::npos) return "";
    ++start;
    
    while (start < body.size() && std::isspace(body[start])) ++start;
    
    if (start >= body.size() || body[start] != '\"') return "";
    ++start;
    
    size_t end = findQuoteEnd(body, start);
    if (end == std::string::npos) return "";
    
    return unescapeJSONString(body.substr(start, end - start));
}

std::string GoogleTranslator::extractTranslationFromFreeAPI(const std::string& body)
{
    const std::string pattern = "[[[\"";
    size_t start = body.find(pattern);
    if (start == std::string::npos) return "";
    
    start += pattern.length();
    
    size_t end = findQuoteEnd(body, start);
    if (end == std::string::npos) return "";
    
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
            if (next == '\"') { unescaped += '\"'; ++i; }
            else if (next == '\\') { unescaped += '\\'; ++i; }
            else if (next == 'n') { unescaped += '\n'; ++i; }
            else if (next == 'r') { unescaped += '\r'; ++i; }
            else if (next == 't') { unescaped += '\t'; ++i; }
            else unescaped += escaped[i];
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
    if (lang_code == "en-us") return "en";
    if (lang_code == "zh-cn") return "zh-cn";
    if (lang_code == "zh-tw") return "zh-tw";
    if (lang_code == "ja-jp") return "ja";
    if (lang_code == "ko-kr") return "ko";
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
