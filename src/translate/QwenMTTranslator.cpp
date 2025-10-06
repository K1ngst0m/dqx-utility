#include "QwenMTTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>

using namespace translate;

QwenMTTranslator::QwenMTTranslator() = default;
QwenMTTranslator::~QwenMTTranslator() { shutdown(); }

bool QwenMTTranslator::init(const TranslatorConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
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
}

bool QwenMTTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
{
    if (!isReady())
    {
        last_error_ = "translator not ready";
        return false;
    }
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
        if (doRequest(j.text, j.dst, out))
        {
            PLOG_INFO << "Qwen-MT Translation [" << j.src << " -> " << j.dst << "]: '" << j.text << "' -> '" << out << "'";
            Completed c; c.id = j.id; c.text = std::move(out);
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

std::string QwenMTTranslator::escapeJSON(const std::string& s)
{
    std::string o; o.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
        case '\\': o += "\\\\"; break;
        case '"':  o += "\\\""; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default: o += c; break;
        }
    }
    return o;
}

std::string QwenMTTranslator::mapTarget(const std::string& dst_lang)
{
    if (dst_lang == "en-us") return "English";
    if (dst_lang == "zh-cn") return "Chinese";
    if (dst_lang == "zh-tw") return "Chinese (Traditional)"; // best-effort label
    return dst_lang;
}

bool QwenMTTranslator::doRequest(const std::string& text, const std::string& dst_lang, std::string& out_text)
{
    if (text.empty()) return false;

    std::string url = cfg_.base_url;

    std::string messages = "[{\\\"role\\\":\\\"user\\\",\\\"content\\\":\\\"" + escapeJSON(text) + "\\\"}]";
    std::string target = mapTarget(dst_lang);

    std::string body;
    body.reserve(512 + text.size());
    body += "{\"model\":\"" + cfg_.model + "\",";
    body += "\"messages\":" + messages + ",";
    body += "\"translation_options\":{\"source_lang\":\"auto\",\"target_lang\":\"" + target + "\"}}";

    cpr::Header headers{{"Content-Type","application/json"},{"Authorization", std::string("Bearer ")+cfg_.api_key}};

    cpr::Session session;
    session.SetUrl(cpr::Url{url});
    session.SetHeader(headers);
    session.SetBody(cpr::Body{body});
    session.SetConnectTimeout(cpr::ConnectTimeout{5000});
    session.SetTimeout(cpr::Timeout{15000});
    session.SetProgressCallback(cpr::ProgressCallback(
        [](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t userdata) -> bool {
            auto flag = reinterpret_cast<std::atomic<bool>*>(userdata);
            return flag && flag->load();
        }, reinterpret_cast<intptr_t>(&running_)));

    auto r = session.Post();
    if (r.error)
    {
        std::string err_msg = r.error.message;
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Qwen-MT request failed: " << err_msg;
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(r.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Qwen-MT request failed with status " << r.status_code << ": " << r.text;
        }
        return false;
    }

    const std::string key = "\"content\"";
    size_t p = r.text.find(key);
    if (p == std::string::npos) return false;
    p = r.text.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < r.text.size() && (r.text[p] == ' ' || r.text[p] == '\t' || r.text[p] == '\n' || r.text[p] == '\r')) ++p;
    if (p >= r.text.size() || r.text[p] != '"') return false;
    ++p;
    std::string v;
    while (p < r.text.size())
    {
        char c = r.text[p++];
        if (c == '\\')
        {
            if (p >= r.text.size()) break;
            char e = r.text[p++];
            if (e == 'n') v.push_back('\n');
            else if (e == 'r') v.push_back('\r');
            else if (e == 't') v.push_back('\t');
            else v.push_back(e);
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
    if (cfg_.model.empty() || cfg_.base_url.empty() || cfg_.api_key.empty())
        return "Error: Missing configuration (model/base URL/API key)";

    std::string result;
    if (!doRequest("Hello", cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang, result))
    {
        if (last_error_.empty()) return "Error: Test translation failed";
        return std::string("Error: Test translation failed - ") + last_error_;
    }
    if (result.empty()) return "Error: Test translation returned empty result";
    return "Success: Qwen-MT connection test passed";
}
