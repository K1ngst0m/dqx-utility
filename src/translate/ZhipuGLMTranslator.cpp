#include "ZhipuGLMTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>

using namespace translate;

ZhipuGLMTranslator::ZhipuGLMTranslator() = default;
ZhipuGLMTranslator::~ZhipuGLMTranslator() { shutdown(); }

bool ZhipuGLMTranslator::init(const TranslatorConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
    running_.store(true);
    worker_ = std::thread(&ZhipuGLMTranslator::workerLoop, this);
    return true;
}

bool ZhipuGLMTranslator::isReady() const
{
    return running_.load() && !cfg_.model.empty() && !cfg_.base_url.empty() && !cfg_.api_key.empty();
}

void ZhipuGLMTranslator::shutdown()
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

bool ZhipuGLMTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
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

bool ZhipuGLMTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void ZhipuGLMTranslator::workerLoop()
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
            PLOG_INFO << "GLM Translation [" << j.src << " -> " << j.dst << "]: '" << j.text << "' -> '" << out << "'";
            Completed c; c.id = j.id; c.text = std::move(out);
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

std::string ZhipuGLMTranslator::escapeJSON(const std::string& s)
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

bool ZhipuGLMTranslator::extractContent(const std::string& body, std::string& out)
{
    size_t p = body.find("\"choices\"");
    if (p == std::string::npos) return false;
    p = body.find("\"message\"", p);
    if (p == std::string::npos) return false;
    p = body.find("\"content\"", p);
    if (p == std::string::npos) return false;
    p = body.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\n' || body[p] == '\r')) ++p;
    if (p >= body.size() || body[p] != '"') return false;
    ++p;
    std::string v;
    while (p < body.size())
    {
        char c = body[p++];
        if (c == '\\')
        {
            if (p >= body.size()) break;
            char e = body[p++];
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
    out.swap(v);
    return true;
}

bool ZhipuGLMTranslator::doRequest(const std::string& text, const std::string& target_lang, std::string& out_text)
{
    if (text.empty()) return false;
    std::string url = cfg_.base_url;

    std::string target_name;
    if (target_lang == "en-us") target_name = "English";
    else if (target_lang == "zh-cn") target_name = "Simplified Chinese";
    else if (target_lang == "zh-tw") target_name = "Traditional Chinese";
    else target_name = target_lang;

    std::string sys = "Translate the following game dialog to " + target_name + ". Keep the speaker's tone and game style. Preserve any <...> tags exactly. Do not add or remove content.";
    std::string user = text;

    std::string body;
    body.reserve(512 + user.size());
    body += "{\"model\":\"" + cfg_.model + "\",";
    body += "\"messages\":[";
    body += "{\"role\":\"system\",\"content\":\"" + escapeJSON(sys) + "\"},";
    body += "{\"role\":\"user\",\"content\":\"" + escapeJSON(user) + "\"}],";
    body += "\"temperature\":0.3,\"top_p\":0.7,\"stream\":false}";

    cpr::Header headers{{"Content-Type","application/json"}};
    if (!cfg_.api_key.empty()) {
        headers.insert({"Authorization", std::string("Bearer ") + cfg_.api_key});
    }

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
            PLOG_WARNING << "ZhipuGLM request failed: " << err_msg;
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(r.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "ZhipuGLM request failed with status " << r.status_code << ": " << r.text;
        }
        return false;
    }

    std::string content;
    if (!extractContent(r.text, content))
    {
        std::string err_msg = "parse error";
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "ZhipuGLM response parse failed: " << r.text;
        }
        return false;
    }
    out_text = std::move(content);
    return true;
}

std::string ZhipuGLMTranslator::testConnection()
{
    // Minimal test by attempting a short request (a real /models endpoint may not be available)
    if (cfg_.model.empty() || cfg_.base_url.empty())
        return "Error: Missing configuration (model or base URL)";

    std::string result;
    if (!doRequest("Hello", cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang, result))
    {
        if (last_error_.empty()) return "Error: Test translation failed";
        return std::string("Error: Test translation failed - ") + last_error_;
    }
    if (result.empty()) return "Error: Test translation returned empty result";
    return "Success: GLM-4 Flash connection test passed";
}
