#include "NiutransTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>
#include "HttpCommon.hpp"

using namespace translate;

NiutransTranslator::NiutransTranslator() = default;
NiutransTranslator::~NiutransTranslator() { shutdown(); }

bool NiutransTranslator::init(const BackendConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
    running_.store(true);
    worker_ = std::thread(&NiutransTranslator::workerLoop, this);
    return true;
}

bool NiutransTranslator::isReady() const
{
    return running_.load() && !cfg_.api_key.empty();
}

void NiutransTranslator::shutdown()
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

bool NiutransTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
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

bool NiutransTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void NiutransTranslator::workerLoop()
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
            PLOG_INFO << "Niutrans Translation [auto -> " << j.dst << "]: '" << j.text << "' -> '" << out << "'";
            Completed c; c.id = j.id; c.text = std::move(out); c.failed = false;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
        else
        {
            PLOG_WARNING << "Niutrans Translation failed [auto -> " << j.dst << "]: '" << j.text << "' - " << last_error_;
            Completed c; c.id = j.id; c.failed = true; c.original_text = j.text; c.error_message = last_error_;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

std::string NiutransTranslator::mapTarget(const std::string& dst_lang)
{
    if (dst_lang == "en-us") return "en";
    if (dst_lang == "zh-cn") return "zh";
    if (dst_lang == "zh-tw") return "zh-TW";
    return dst_lang;
}

bool NiutransTranslator::doRequest(const std::string& text, const std::string& dst_lang, std::string& out_text)
{
    if (text.empty()) return false;
    std::string url = cfg_.base_url.empty() ? std::string("https://api.niutrans.com/NiuTransServer/translation") : cfg_.base_url;

    std::vector<std::pair<std::string, std::string>> fields{
        {"from", "auto"},
        {"to", mapTarget(dst_lang)},
        {"apikey", cfg_.api_key},
        {"src_text", text}
    };

    translate::SessionConfig scfg;
    scfg.connect_timeout_ms = 5000;
    scfg.timeout_ms = 15000;
    scfg.cancel_flag = &running_;
    auto resp = translate::post_form(url, fields, scfg);
    if (!resp.error.empty())
    {
        std::string err_msg = resp.error;
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Niutrans request failed: " << err_msg;
        }
        return false;
    }
    if (resp.status_code < 200 || resp.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(resp.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Niutrans request failed with status " << resp.status_code << ": " << resp.text;
        }
        return false;
    }

    const std::string key = "\"tgt_text\"";
    size_t p = resp.text.find(key);
    if (p == std::string::npos)
    {
        // Try error_msg
        size_t em = resp.text.find("\"error_msg\"");
        if (em != std::string::npos)
        {
            size_t c = resp.text.find(':', em); if (c != std::string::npos) {
                ++c; while (c < resp.text.size() && (resp.text[c]==' '||resp.text[c]=='\t')) ++c;
                if (c<resp.text.size() && resp.text[c]=='"') { ++c; std::string msg; while (c<resp.text.size()) { char ch=resp.text[c++]; if (ch=='\\' && c<resp.text.size()) { msg.push_back(resp.text[c++]); } else if (ch=='"') break; else msg.push_back(ch);} last_error_ = msg; }
            }
        }
        return false;
    }
    p = resp.text.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < resp.text.size() && (resp.text[p] == ' ' || resp.text[p] == '\t' || resp.text[p] == '\n' || resp.text[p] == '\r')) ++p;
    if (p >= resp.text.size() || resp.text[p] != '"') return false;
    ++p;
    std::string v;
    while (p < resp.text.size())
    {
        char c = resp.text[p++];
        if (c == '\\')
        {
            if (p >= resp.text.size()) break;
            char e = resp.text[p++];
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

std::string NiutransTranslator::testConnection()
{
    if (cfg_.api_key.empty())
        return "Error: Missing API key";
    std::string result;
    if (cfg_.base_url.empty()) cfg_.base_url = "https://api.niutrans.com/NiuTransServer/translation";
    if (!doRequest("Hello", cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang, result))
    {
        if (last_error_.empty()) return "Error: Test translation failed";
        return std::string("Error: Test translation failed - ") + last_error_;
    }
    if (result.empty()) return "Error: Test translation returned empty result";
    return "Success: Niutrans connection test passed";
}
