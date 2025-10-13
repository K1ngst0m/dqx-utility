#include "OpenAITranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>
#include "HttpCommon.hpp"

using namespace translate;

OpenAITranslator::OpenAITranslator() = default;
OpenAITranslator::~OpenAITranslator() { shutdown(); }

bool OpenAITranslator::init(const TranslatorConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();
    running_.store(true);
    worker_ = std::thread(&OpenAITranslator::workerLoop, this);
    return true;
}

bool OpenAITranslator::isReady() const
{
    return running_.load() && !cfg_.api_key.empty() && !cfg_.model.empty() && !cfg_.base_url.empty();
}

void OpenAITranslator::shutdown()
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

bool OpenAITranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
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

bool OpenAITranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void OpenAITranslator::workerLoop()
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

std::string OpenAITranslator::escapeJSON(const std::string& s)
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

bool OpenAITranslator::extractContent(const std::string& body, std::string& out)
{
    const std::string key = "\"content\"";
    size_t p = body.find(key);
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

std::string OpenAITranslator::normalizeURL(const std::string& base_url)
{
    std::string url = base_url;
    
    // Remove trailing slash
    while (!url.empty() && url.back() == '/') 
        url.pop_back();
    
    if (url.empty())
        return url;
    
    // Check if URL already contains a path beyond the domain
    // If it has more than just domain (e.g., contains /v1beta, /api, etc.), use it exactly
    size_t scheme_end = url.find("://");
    size_t path_start = (scheme_end != std::string::npos) 
        ? url.find('/', scheme_end + 3) 
        : url.find('/');
    
    // If there's a path component
    if (path_start != std::string::npos)
    {
        std::string path = url.substr(path_start);
        
        // Only handle these 3 patterns:
        // 1. Exact match: xxx/v1/chat/completions - use as-is
        if (path.find("/v1/chat/completions") != std::string::npos)
            return url;
        
        // 2. Ends with /v1 - append /chat/completions
        if (path == "/v1")
            return url + "/chat/completions";
        
        // 3. Any other path - use exactly as provided (e.g., /v1beta, /api/v2, etc.)
        return url;
    }
    
    // No path component (just domain like "https://api.openai.com")
    // Append the standard OpenAI path
    return url + "/v1/chat/completions";
}

bool OpenAITranslator::doRequest(const std::string& text, const std::string& target_lang, std::string& out_text)
{
    if (text.empty()) return false;
    std::string url = normalizeURL(cfg_.base_url);

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
    body += "\"temperature\":0.3}";

    std::vector<translate::Header> headers{{"Content-Type","application/json"},{"Authorization", std::string("Bearer ")+cfg_.api_key}};
    translate::SessionConfig scfg; scfg.connect_timeout_ms = 5000; scfg.timeout_ms = 45000; scfg.cancel_flag = &running_;
    auto r = translate::post_json(url, body, headers, scfg);
    if (!r.error.empty())
    {
        std::string err_msg = r.error;
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Translation request failed: " << err_msg;
        }
        return false;
    }
    if (r.status_code < 200 || r.status_code >= 300)
    {
        std::string err_msg = std::string("http ") + std::to_string(r.status_code);
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Translation request failed with status " << r.status_code << ": " << r.text;
        }
        return false;
    }
    // Extract translated text from OpenAI JSON response
    std::string content;
    if (!extractContent(r.text, content))
    {
        std::string err_msg = "parse error";
        if (last_error_ != err_msg)
        {
            last_error_ = err_msg;
            PLOG_WARNING << "Translation response parse failed: " << r.text;
        }
        return false;
    }
    out_text = std::move(content);
    return true;
}

std::string OpenAITranslator::testConnection()
{
    if (cfg_.api_key.empty())
        return "Config Error: Missing API key";
    if (cfg_.base_url.empty())
        return "Config Error: Missing base URL";
    if (cfg_.model.empty())
        return "Config Error: Missing model";
    
    // Step 1: Check base URL connection
    std::string models_url = cfg_.base_url;
    if (!models_url.empty() && models_url.back() == '/') 
        models_url.pop_back();
    models_url += "/v1/models";
    
    cpr::Header auth_headers{{"Authorization", std::string("Bearer ") + cfg_.api_key}};
    {
        cpr::Session s;
        s.SetUrl(cpr::Url{models_url});
        s.SetHeader(auth_headers);
        s.SetConnectTimeout(cpr::ConnectTimeout{3000});
        s.SetTimeout(cpr::Timeout{8000});
        s.SetProgressCallback(cpr::ProgressCallback(
            [](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t userdata) -> bool {
                auto flag = reinterpret_cast<std::atomic<bool>*>(userdata);
                return flag && flag->load();
            }, reinterpret_cast<intptr_t>(&running_)));
        auto models_response = s.Get();
        if (models_response.error)
            return "Error: Cannot connect to base URL - " + models_response.error.message;
        if (models_response.status_code < 200 || models_response.status_code >= 300)
            return "Error: Base URL returned HTTP " + std::to_string(models_response.status_code);
        // proceed using models_response below
        bool model_found = models_response.text.find('"' + cfg_.model + '"') != std::string::npos;
        if (!model_found)
            return "Warning: Model '" + cfg_.model + "' not found in available models list";
    }
    
    
    // Step 3: Simple translation test
    std::string test_text = "Hello";
    std::string target_lang = cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang;
    std::string result;
    
    if (!doRequest(test_text, target_lang, result))
        return "Error: Test translation failed - " + last_error_;
    
    if (result.empty())
        return "Error: Test translation returned empty result";
    
    return "Success: Connection test passed, model responded correctly";
}
