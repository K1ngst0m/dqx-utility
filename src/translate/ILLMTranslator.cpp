#include "ILLMTranslator.hpp"

#include <plog/Log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace translate
{

ILLMTranslator::ILLMTranslator() = default;

ILLMTranslator::~ILLMTranslator()
{
    shutdown();
}

bool ILLMTranslator::init(const BackendConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    last_error_.clear();

    const auto validation_error = validateConfig(cfg_);
    if (!validation_error.empty())
    {
        last_error_ = validation_error;
        return false;
    }

    onInit();

    max_concurrent_requests_ = cfg_.max_concurrent_requests == 0 ? 1 : cfg_.max_concurrent_requests;
    request_interval_seconds_ = cfg_.request_interval_seconds < 0.0 ? 0.0 : cfg_.request_interval_seconds;
    max_retries_ = cfg_.max_retries < 0 ? 0 : cfg_.max_retries;

    in_flight_.store(0, std::memory_order_relaxed);
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    last_request_ = std::chrono::steady_clock::now() -
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);

    running_.store(true, std::memory_order_relaxed);
    worker_ = std::thread(&ILLMTranslator::workerLoop, this);
    return true;
}

bool ILLMTranslator::isReady() const
{
    return running_.load(std::memory_order_relaxed) && hasValidRuntimeConfig();
}

void ILLMTranslator::shutdown()
{
    running_.store(false, std::memory_order_relaxed);
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

bool ILLMTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang,
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

    Job job;
    job.id = next_id_.fetch_add(1, std::memory_order_relaxed);
    job.text = text;
    job.src = src_lang;
    job.dst = dst_lang;
    const auto queued_id = job.id;

    {
        std::lock_guard<std::mutex> lk(q_mtx_);
        queue_.push(std::move(job));
    }

    out_id = queued_id;
    return true;
}

bool ILLMTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

std::string ILLMTranslator::testConnection()
{
    return testConnectionImpl();
}

void ILLMTranslator::workerLoop()
{
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    while (running_.load(std::memory_order_relaxed))
    {
        Job job;
        {
            std::lock_guard<std::mutex> lk(q_mtx_);
            if (!queue_.empty())
            {
                job = std::move(queue_.front());
                queue_.pop();
            }
        }

        if (job.id == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        in_flight_.fetch_add(1, std::memory_order_relaxed);
        FlightGuard guard(in_flight_);

        bool success = false;
        RequestResult request_result;
        int attempt = 0;

        while (running_.load(std::memory_order_relaxed))
        {
            if (interval.count() > 0.0)
            {
                std::chrono::steady_clock::time_point wait_until;
                {
                    std::lock_guard<std::mutex> lock(rate_mtx_);
                    wait_until =
                        last_request_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
                }

                const auto now = std::chrono::steady_clock::now();
                if (wait_until > now)
                {
                    std::this_thread::sleep_for(wait_until - now);
                    if (!running_.load(std::memory_order_relaxed))
                        break;
                }
            }

            request_result = performRequest(job);

            {
                std::lock_guard<std::mutex> lock(rate_mtx_);
                last_request_ = std::chrono::steady_clock::now();
            }

            if (request_result.success)
            {
                success = true;
                break;
            }

            last_error_ = request_result.error_message;

            if (!request_result.retryable || attempt >= max_retries_)
                break;

            ++attempt;
            auto backoff_ms = static_cast<int>(200 * attempt);
            if (request_result.retry_after_seconds > 0.0)
            {
                backoff_ms = std::max(backoff_ms, static_cast<int>(request_result.retry_after_seconds * 1000.0));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }

        if (success)
        {
            PLOG_DEBUG << providerName() << " translation [" << job.src << " -> " << job.dst << "]: '" << job.text
                      << "' -> '" << request_result.completed.text << "'";
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(request_result.completed));
        }
        else
        {
            PLOG_WARNING << providerName() << " translation failed [" << job.src << " -> " << job.dst << "]: '"
                         << job.text << "' - " << last_error_;
            Completed failed;
            failed.id = job.id;
            failed.failed = true;
            failed.original_text = job.text;
            failed.error_message = last_error_;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(failed));
        }
    }
}

void ILLMTranslator::onInit() {}

std::string ILLMTranslator::validateConfig(const BackendConfig&) const
{
    return {};
}

ILLMTranslator::ProviderLimits ILLMTranslator::providerLimits() const
{
    return {};
}

bool ILLMTranslator::hasValidRuntimeConfig() const
{
    return true;
}

bool ILLMTranslator::shouldRetry(const HttpResponse& resp) const
{
    if (!resp.error.empty())
        return true;
    if (resp.status_code == 429)
        return true;
    return resp.status_code >= 500 || resp.status_code == 408 || resp.status_code == 0;
}

void ILLMTranslator::augmentPromptContext(const Job&, PromptContext&) const {}

void ILLMTranslator::configureSession(const Job&, SessionConfig& cfg) const
{
    cfg.connect_timeout_ms = 5000;
    cfg.timeout_ms = 45000;
}

std::string ILLMTranslator::connectionSuccessMessage() const
{
    return std::string("Success: ") + providerName() + " connection test passed";
}

std::string ILLMTranslator::testConnectionImpl()
{
    Job job;
    job.id = 0;
    job.text = "Hello";
    job.src = cfg_.target_lang.empty() ? "auto" : cfg_.target_lang;
    job.dst = cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang;

    const auto result = performRequest(job);
    if (result.success && !result.completed.text.empty())
        return connectionSuccessMessage();

    if (!result.error_message.empty())
        return "Error: Test translation failed - " + result.error_message;

    return "Error: Test translation failed";
}

ILLMTranslator::Prompt ILLMTranslator::buildPrompt(const Job& job) const
{
    PromptContext ctx;
    ctx.source_lang = job.src;
    ctx.target_lang = job.dst.empty() ? cfg_.target_lang : job.dst;
    if (ctx.target_lang.empty())
        ctx.target_lang = "zh-cn";
    ctx.replacements.emplace_back("{target_lang}", languageDisplayName(ctx.target_lang));
    ctx.replacements.emplace_back("{source_lang}", languageDisplayName(ctx.source_lang));
    std::string glossary_block = buildGlossarySnippet(job, ctx.target_lang);
    if (glossary_block.empty())
        glossary_block = "(empty)";
    ctx.replacements.emplace_back("{glossary}", glossary_block);
    ctx.replacements.emplace_back("{source_text}", job.text);
    augmentPromptContext(job, ctx);

    const std::string default_template = std::string(R"(You are a professional translator familiar with the Dragon Quest series.
Translate the following Dragon Quest X dialogue into {target_lang}.
Preserve official DQX terminology and tone; when no official wording exists, lean on standard series phrasing.
Adhere to the glossary below when available:
{glossary}

Guidelines:
- Stay faithful to the source; add nothing and omit nothing.
- Retain the speaker's voice, era flavor, and the series' stylistic quirks.
- Output translation only—no explanations.
Source text:
{source_text})");
    std::string system_prompt = cfg_.prompt.empty() ? default_template : cfg_.prompt;
    for (const auto& repl : ctx.replacements)
        replaceAll(system_prompt, repl.first, repl.second);

    Prompt prompt;
    if (!system_prompt.empty())
        prompt.messages.push_back({ Role::System, std::move(system_prompt) });
    prompt.messages.push_back({ Role::User, job.text });
    return prompt;
}

std::string ILLMTranslator::languageDisplayName(const std::string& lang)
{
    std::string lower;
    lower.reserve(lang.size());
    for (char c : lang)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "en" || lower == "en-us" || lower == "en_us")
        return "English";
    if (lower == "zh-cn" || lower == "zh-hans")
        return "Simplified Chinese";
    if (lower == "zh-tw" || lower == "zh-hant")
        return "Traditional Chinese";
    if (lower == "ja" || lower == "ja-jp")
        return "Japanese";
    return lang.empty() ? "target language" : lang;
}

void ILLMTranslator::replaceAll(std::string& target, const std::string& placeholder, const std::string& value)
{
    if (placeholder.empty())
        return;
    size_t pos = 0;
    while ((pos = target.find(placeholder, pos)) != std::string::npos)
    {
        target.replace(pos, placeholder.length(), value);
        pos += value.length();
    }
}

std::string ILLMTranslator::buildGlossarySnippet(const Job& job, const std::string& target_lang) const
{
    if (!cfg_.glossary_enabled)
        return {};
    const std::string effective_lang = target_lang.empty() ? cfg_.target_lang : target_lang;
    auto& manager = sharedGlossaryManager();
    return manager.buildGlossarySnippet(job.text, effective_lang.empty() ? "zh-cn" : effective_lang, 10);
}

processing::GlossaryManager& ILLMTranslator::sharedGlossaryManager()
{
    static processing::GlossaryManager manager;
    static std::once_flag once;
    std::call_once(once, []() { manager.initialize(); });
    return manager;
}

ILLMTranslator::RequestResult ILLMTranslator::performRequest(const Job& job)
{
    RequestResult result;

    const auto limits = providerLimits();
    if (limits.max_input_bytes > 0)
    {
        auto length_check = helpers::check_text_length(job.text, limits.max_input_bytes, providerName());
        if (!length_check.ok)
        {
            result.error_message = length_check.error_message;
            return result;
        }
    }

    auto prompt = buildPrompt(job);
    nlohmann::json body_json = nlohmann::json::object();
    buildRequestBody(job, prompt, body_json);
    std::string body = body_json.dump();
    PLOG_DEBUG << "final post body: " << body;

    std::vector<Header> headers;
    buildHeaders(job, headers);

    SessionConfig session_cfg;
    session_cfg.cancel_flag = &running_;
    session_cfg.text_length_hint = job.text.size();
    configureSession(job, session_cfg);

    const auto url = buildUrl(job);
    const auto response = translate::post_json(url, body, headers, session_cfg);

    if (!response.error.empty() || response.status_code < 200 || response.status_code >= 300)
    {
        auto err_type = helpers::categorize_http_error(response.status_code, response.error);
        const std::string snippet = !response.error.empty() ? response.error : response.text;
        result.error_message = helpers::get_error_description(err_type, response.status_code, snippet);
        result.retryable = shouldRetry(response);
        PLOG_WARNING << providerName() << " request failed: " << result.error_message;
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                            std::string(providerName()) + " request failed", result.error_message);
        return result;
    }

    Completed completed;
    completed.id = job.id;
    completed.original_text = job.text;
    completed.failed = false;

    auto parse = parseResponse(job, response, completed);
    if (!parse.ok)
    {
        result.error_message = parse.error_message.empty() ? "parse error" : parse.error_message;
        result.retryable = parse.retryable;
        result.retry_after_seconds = parse.retry_after_seconds;
        PLOG_WARNING << providerName() << " response parse failed: " << result.error_message;
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation,
                                            std::string(providerName()) + " response parse failed",
                                            result.error_message);
        return result;
    }

    result.success = true;
    result.completed = std::move(completed);
    return result;
}

} // namespace translate
