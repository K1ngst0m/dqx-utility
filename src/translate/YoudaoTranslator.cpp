#include "YoudaoTranslator.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>
#include "HttpCommon.hpp"
#include "../utils/ErrorReporter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace translate;

namespace {

struct FlightGuard {
    std::atomic<std::size_t>& ref;
    explicit FlightGuard(std::atomic<std::size_t>& r) : ref(r) {}
    ~FlightGuard() { ref.fetch_sub(1, std::memory_order_relaxed); }
};

constexpr std::array<std::uint32_t, 64> kSha256Table = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline void report_youdao_error(std::string& last_error, const char* user_message, const std::string& details)
{
    if (details.empty())
        return;
    if (last_error == details)
        return;
    last_error = details;
    utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Translation, user_message, details);
}

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

struct Sha256Ctx {
    std::array<std::uint32_t, 8> state{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    std::array<std::uint8_t, 64> buffer{};
    std::uint64_t bitlen = 0;
    std::size_t buffer_len = 0;
};

void sha256_transform(Sha256Ctx& ctx, const std::uint8_t* data)
{
    std::uint32_t w[64];
    for (std::size_t i = 0; i < 16; ++i)
    {
        std::size_t j = i * 4;
        w[i] = (static_cast<std::uint32_t>(data[j]) << 24) |
               (static_cast<std::uint32_t>(data[j + 1]) << 16) |
               (static_cast<std::uint32_t>(data[j + 2]) << 8) |
               static_cast<std::uint32_t>(data[j + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i)
    {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = ctx.state[0];
    std::uint32_t b = ctx.state[1];
    std::uint32_t c = ctx.state[2];
    std::uint32_t d = ctx.state[3];
    std::uint32_t e = ctx.state[4];
    std::uint32_t f = ctx.state[5];
    std::uint32_t g = ctx.state[6];
    std::uint32_t h = ctx.state[7];

    for (std::size_t i = 0; i < 64; ++i)
    {
        std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch = (e & f) ^ ((~e) & g);
        std::uint32_t temp1 = h + S1 + ch + kSha256Table[i] + w[i];
        std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void sha256_update(Sha256Ctx& ctx, const std::uint8_t* data, std::size_t len)
{
    while (len > 0)
    {
        std::size_t to_copy = std::min<std::size_t>(len, 64 - ctx.buffer_len);
        std::memcpy(ctx.buffer.data() + ctx.buffer_len, data, to_copy);
        ctx.buffer_len += to_copy;
        data += to_copy;
        len -= to_copy;

        if (ctx.buffer_len == 64)
        {
            sha256_transform(ctx, ctx.buffer.data());
            ctx.bitlen += 512;
            ctx.buffer_len = 0;
        }
    }
}

void sha256_final(Sha256Ctx& ctx, std::uint8_t hash[32])
{
    ctx.bitlen += static_cast<std::uint64_t>(ctx.buffer_len) * 8;
    ctx.buffer[ctx.buffer_len++] = 0x80;

    if (ctx.buffer_len > 56)
    {
        while (ctx.buffer_len < 64)
            ctx.buffer[ctx.buffer_len++] = 0;
        sha256_transform(ctx, ctx.buffer.data());
        ctx.buffer_len = 0;
    }

    while (ctx.buffer_len < 56)
        ctx.buffer[ctx.buffer_len++] = 0;

    for (int i = 7; i >= 0; --i)
        ctx.buffer[ctx.buffer_len++] = static_cast<std::uint8_t>((ctx.bitlen >> (8 * i)) & 0xFFu);

    sha256_transform(ctx, ctx.buffer.data());

    for (std::size_t i = 0; i < 4; ++i)
    {
        hash[i]      = static_cast<std::uint8_t>((ctx.state[0] >> (24 - i * 8)) & 0xFFu);
        hash[i + 4]  = static_cast<std::uint8_t>((ctx.state[1] >> (24 - i * 8)) & 0xFFu);
        hash[i + 8]  = static_cast<std::uint8_t>((ctx.state[2] >> (24 - i * 8)) & 0xFFu);
        hash[i + 12] = static_cast<std::uint8_t>((ctx.state[3] >> (24 - i * 8)) & 0xFFu);
        hash[i + 16] = static_cast<std::uint8_t>((ctx.state[4] >> (24 - i * 8)) & 0xFFu);
        hash[i + 20] = static_cast<std::uint8_t>((ctx.state[5] >> (24 - i * 8)) & 0xFFu);
        hash[i + 24] = static_cast<std::uint8_t>((ctx.state[6] >> (24 - i * 8)) & 0xFFu);
        hash[i + 28] = static_cast<std::uint8_t>((ctx.state[7] >> (24 - i * 8)) & 0xFFu);
    }
}

} // namespace

YoudaoTranslator::YoudaoTranslator() = default;
YoudaoTranslator::~YoudaoTranslator()
{
    shutdown();
}

bool YoudaoTranslator::init(const BackendConfig& cfg)
{
    shutdown();
    cfg_ = cfg;
    cfg_.api_key = trimCopy(cfg_.api_key);
    cfg_.api_secret = trimCopy(cfg_.api_secret);
    cfg_.model = trimCopy(cfg_.model);
    cfg_.base_url = trimCopy(cfg_.base_url);
    last_error_.clear();
    max_concurrent_requests_ = cfg_.max_concurrent_requests == 0 ? 1 : cfg_.max_concurrent_requests;
    request_interval_seconds_ = cfg_.request_interval_seconds < 0.0 ? 0.0 : cfg_.request_interval_seconds;
    max_retries_ = cfg_.max_retries < 0 ? 0 : cfg_.max_retries;
    in_flight_.store(0, std::memory_order_relaxed);
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    last_request_ = std::chrono::steady_clock::now() - std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
    mode_ = (cfg_.model == "youdao_large") ? Mode::LargeModel : Mode::Text;
    running_.store(true);
    worker_ = std::thread(&YoudaoTranslator::workerLoop, this);
    return true;
}

bool YoudaoTranslator::isReady() const
{
    if (!running_.load())
        return false;
    if (cfg_.api_key.empty() || cfg_.api_secret.empty())
        return false;
    return true;
}

void YoudaoTranslator::shutdown()
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

bool YoudaoTranslator::translate(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::uint64_t& out_id)
{
    if (!isReady())
    {
        last_error_ = "translator not ready";
        return false;
    }

    bool all_space = true;
    for (unsigned char c : text)
    {
        if (!std::isspace(c))
        {
            all_space = false;
            break;
        }
    }
    if (text.empty() || all_space)
        return false;

    Job job;
    job.id = next_id_++;
    job.text = text;
    job.src = src_lang;
    job.dst = dst_lang;
    {
        std::lock_guard<std::mutex> lk(q_mtx_);
        queue_.push(job);
    }
    out_id = job.id;
    return true;
}

bool YoudaoTranslator::drain(std::vector<Completed>& out)
{
    std::lock_guard<std::mutex> lk(r_mtx_);
    if (results_.empty())
        return false;
    out.swap(results_);
    return true;
}

void YoudaoTranslator::workerLoop()
{
    const auto interval = std::chrono::duration<double>(request_interval_seconds_);
    while (running_.load())
    {
        Job job;
        {
            std::lock_guard<std::mutex> lk(q_mtx_);
            if (!queue_.empty())
            {
                job = queue_.front();
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
        std::string translated;
        int attempt = 0;
        while (running_.load())
        {
            if (interval.count() > 0.0)
            {
                std::chrono::steady_clock::time_point wait_until;
                {
                    std::lock_guard<std::mutex> lock(rate_mtx_);
                    wait_until = last_request_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
                }
                auto now = std::chrono::steady_clock::now();
                if (wait_until > now)
                {
                    std::this_thread::sleep_for(wait_until - now);
                    if (!running_.load())
                        break;
                }
            }

            bool ok = (mode_ == Mode::LargeModel)
                ? doLargeModelRequest(job.text, job.src, job.dst, translated)
                : doTextRequest(job.text, job.src, job.dst, translated);

            {
                std::lock_guard<std::mutex> lock(rate_mtx_);
                last_request_ = std::chrono::steady_clock::now();
            }

            if (ok)
            {
                success = true;
                break;
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
            PLOG_INFO << "Youdao Translation [" << job.src << " -> " << job.dst << "]: '" << job.text << "' -> '" << translated << "'";
            Completed c;
            c.id = job.id;
            c.text = std::move(translated);
            c.failed = false;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
        else
        {
            PLOG_WARNING << "Youdao Translation failed [" << job.src << " -> " << job.dst << "]: '" << job.text << "' - " << last_error_;
            Completed c;
            c.id = job.id;
            c.failed = true;
            c.original_text = job.text;
            c.error_message = last_error_;
            std::lock_guard<std::mutex> lk(r_mtx_);
            results_.push_back(std::move(c));
        }
    }
}

bool YoudaoTranslator::doTextRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text)
{
    const std::string url = cfg_.base_url.empty() ? std::string("https://openapi.youdao.com/api") : cfg_.base_url;
    std::string from = mapSource(src_lang, Mode::Text);
    std::string to = mapTarget(dst_lang, Mode::Text);
    if (to.empty())
    {
        report_youdao_error(last_error_, "Youdao text translation unsupported target", "unsupported target language");
        return false;
    }

    std::string salt = makeSalt();
    std::string curtime = makeCurtime();
    std::string input = truncateInput(text);
    std::string sign = buildSignature(cfg_.api_key, cfg_.api_secret, input, salt, curtime);

    std::vector<std::pair<std::string,std::string>> fields{
        {"q", text},
        {"from", from},
        {"to", to},
        {"appKey", cfg_.api_key},
        {"salt", salt},
        {"signType", "v3"},
        {"curtime", curtime},
        {"sign", sign}
    };
    translate::SessionConfig scfg; scfg.connect_timeout_ms = 5000; scfg.timeout_ms = 15000; scfg.cancel_flag = &running_;
    auto response = translate::post_form(url, fields, scfg);
    if (!response.error.empty())
    {
        if (last_error_ != response.error)
        {
            PLOG_WARNING << "Youdao text request failed: " << response.error;
        }
        report_youdao_error(last_error_, "Youdao text request failed", response.error);
        return false;
    }
    if (response.status_code < 200 || response.status_code >= 300)
    {
        std::string err_detail = std::string("http ") + std::to_string(response.status_code) + ": " + response.text;
        if (last_error_ != err_detail)
        {
            PLOG_WARNING << "Youdao text request failed with status " << response.status_code << ": " << response.text;
        }
        report_youdao_error(last_error_, "Youdao text HTTP error", err_detail);
        return false;
    }

    if (!parseTextResponse(response.text, out_text))
        return false;
    return true;
}

bool YoudaoTranslator::doLargeModelRequest(const std::string& text, const std::string& src_lang, const std::string& dst_lang, std::string& out_text)
{
    const std::string url = cfg_.base_url.empty() ? std::string("https://openapi.youdao.com/llm_trans") : cfg_.base_url;
    std::string from = mapSource(src_lang, Mode::LargeModel);
    std::string to = mapTarget(dst_lang, Mode::LargeModel);
    if (to.empty())
    {
        report_youdao_error(last_error_, "Youdao large model unsupported target", "unsupported target language");
        return false;
    }

    if (!dst_lang.empty())
    {
        std::string lower;
        lower.reserve(dst_lang.size());
        for (char ch : dst_lang)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        if (lower == "zh-tw" || lower == "zh-hk")
        {
            std::call_once(trad_warn_once_, [] {
                PLOG_WARNING << "Youdao large model mode does not support Traditional Chinese directly; using Simplified Chinese target.";
            });
        }
    }

    std::string salt = makeSalt();
    std::string curtime = makeCurtime();
    std::string input = truncateInput(text);
    std::string sign = buildSignature(cfg_.api_key, cfg_.api_secret, input, salt, curtime);

    std::vector<std::pair<std::string,std::string>> fields{
        {"i", text},
        {"from", from},
        {"to", to},
        {"appKey", cfg_.api_key},
        {"salt", salt},
        {"signType", "v3"},
        {"curtime", curtime},
        {"sign", sign},
        {"handleOption", "2"},
        {"streamType", "full"}
    };
    translate::SessionConfig scfg; scfg.connect_timeout_ms = 5000; scfg.timeout_ms = 20000; scfg.cancel_flag = &running_;
    std::vector<translate::Header> headers{{"Accept","text/event-stream"}};
    auto response = translate::post_form(url, fields, scfg, headers);
    if (!response.error.empty())
    {
        if (last_error_ != response.error)
        {
            PLOG_WARNING << "Youdao large model request failed: " << response.error;
        }
        report_youdao_error(last_error_, "Youdao large model request failed", response.error);
        return false;
    }
    if (response.status_code < 200 || response.status_code >= 300)
    {
        std::string err_detail = std::string("http ") + std::to_string(response.status_code) + ": " + response.text;
        if (last_error_ != err_detail)
        {
            PLOG_WARNING << "Youdao large model request failed with status " << response.status_code << ": " << response.text;
        }
        report_youdao_error(last_error_, "Youdao large model HTTP error", err_detail);
        return false;
    }

    if (!parseLargeModelResponse(response.text, out_text))
        return false;
    return true;
}

bool YoudaoTranslator::parseTextResponse(const std::string& body, std::string& out_text)
{
    std::string error_code;
    if (extractJsonString(body, "errorCode", error_code))
    {
        if (error_code != "0")
        {
            PLOG_DEBUG << "Youdao text response error " << error_code << ": " << body;
            report_youdao_error(last_error_, "Youdao text response error", std::string("error code ") + error_code);
            return false;
        }
    }

    const std::string key = "\"translation\"";
    std::size_t pos = body.find(key);
    if (pos == std::string::npos)
    {
        report_youdao_error(last_error_, "Youdao text response parse failed", "missing translation field");
        return false;
    }
    pos = body.find('[', pos);
    if (pos == std::string::npos)
    {
        report_youdao_error(last_error_, "Youdao text response parse failed", "missing translation array");
        return false;
    }
    pos = body.find('"', pos);
    if (pos == std::string::npos)
    {
        report_youdao_error(last_error_, "Youdao text response parse failed", "empty translation");
        return false;
    }
    std::size_t end = pos + 1;
    bool escaped = false;
    std::string value;
    while (end < body.size())
    {
        char ch = body[end++];
        if (escaped)
        {
            if (ch == 'n') value.push_back('\n');
            else if (ch == 'r') value.push_back('\r');
            else if (ch == 't') value.push_back('\t');
            else value.push_back(ch);
            escaped = false;
        }
        else if (ch == '\\')
        {
            escaped = true;
        }
        else if (ch == '"')
        {
            break;
        }
        else
        {
            value.push_back(ch);
        }
    }
    if (value.empty())
    {
        report_youdao_error(last_error_, "Youdao text response parse failed", "empty translation");
        return false;
    }
    out_text = std::move(value);
    return true;
}

bool YoudaoTranslator::parseLargeModelResponse(const std::string& body, std::string& out_text)
{
    std::istringstream stream(body);
    std::string line;
    std::string accum;
    std::string last_full;
    std::string err_code;
    std::string err_message;

    while (std::getline(stream, line))
    {
        if (line.rfind("data:", 0) != 0)
            continue;
        std::string data = trimCopy(line.substr(5));
        if (data.empty() || data == "[DONE]")
            continue;

        if (data.find("\"code\"") != std::string::npos && data.find("\"msg\"") != std::string::npos && data.find("transFull") == std::string::npos)
        {
            extractJsonString(data, "code", err_code);
            extractJsonString(data, "msg", err_message);
            continue;
        }

        std::string full;
        std::string incremental;
        if (extractJsonString(data, "transFull", full) && !full.empty())
            last_full = std::move(full);
        if (extractJsonString(data, "transIncre", incremental) && !incremental.empty())
            accum += incremental;

        if (last_full.empty())
        {
            std::string translation;
            if (extractJsonString(data, "translation", translation) && !translation.empty())
                last_full = std::move(translation);
        }

        if (incremental.empty())
        {
            std::string delta;
            if (extractJsonString(data, "delta", delta) && !delta.empty())
                accum += delta;
        }

        if (last_full.empty())
        {
            std::string target;
            if (extractJsonString(data, "targetText", target) && !target.empty())
                last_full = std::move(target);
        }
    }

    if (!last_full.empty())
    {
        out_text = std::move(last_full);
        return true;
    }
    if (!accum.empty())
    {
        out_text = std::move(accum);
        return true;
    }
    if (!err_code.empty())
    {
        std::string detail = err_code + ": " + err_message;
        report_youdao_error(last_error_, "Youdao large model response error", detail);
        return false;
    }

    PLOG_DEBUG << "Youdao large model response produced empty result: " << body;
    report_youdao_error(last_error_, "Youdao large model response parse failed", "empty result");
    return false;
}

std::string YoudaoTranslator::mapSource(const std::string& lang, Mode mode)
{
    if (lang.empty())
        return "auto";
    std::string lower;
    lower.reserve(lang.size());
    for (char ch : lang)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    if (lower == "auto")
        return "auto";
    if (lower == "en-us" || lower == "en")
        return "en";
    if (lower == "zh-cn" || lower == "zh-hans" || lower == "zh-sg")
        return "zh-CHS";
    if (mode == Mode::Text && (lower == "zh-tw" || lower == "zh-hk" || lower == "zh-cht"))
        return "zh-CHT";
    if (mode == Mode::LargeModel && (lower == "zh-tw" || lower == "zh-hk" || lower == "zh-cht"))
        return "zh-CHS";
    return lower;
}

std::string YoudaoTranslator::mapTarget(const std::string& lang, Mode mode)
{
    if (lang.empty())
        return mode == Mode::LargeModel ? std::string("en") : std::string("en");
    std::string lower;
    lower.reserve(lang.size());
    for (char ch : lang)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    if (lower == "en-us" || lower == "en")
        return "en";
    if (lower == "zh-cn" || lower == "zh-hans" || lower == "zh-sg")
        return "zh-CHS";
    if (mode == Mode::Text && (lower == "zh-tw" || lower == "zh-hk" || lower == "zh-cht"))
        return "zh-CHT";
    if (mode == Mode::LargeModel && (lower == "zh-tw" || lower == "zh-hk" || lower == "zh-cht"))
        return "zh-CHS";
    if (mode == Mode::LargeModel)
        return {};
    return lower;
}

namespace {

std::size_t utf8CharLength(unsigned char lead)
{
    if ((lead & 0x80u) == 0)
        return 1;
    if ((lead & 0xE0u) == 0xC0u)
        return 2;
    if ((lead & 0xF0u) == 0xE0u)
        return 3;
    if ((lead & 0xF8u) == 0xF0u)
        return 4;
    return 1;
}

bool isValidContinuation(unsigned char byte)
{
    return (byte & 0xC0u) == 0x80u;
}

} // namespace

std::string YoudaoTranslator::truncateInput(const std::string& text)
{
    std::vector<std::pair<std::size_t, std::size_t>> slices;
    slices.reserve(text.size());

    for (std::size_t i = 0; i < text.size();)
    {
        unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t char_len = utf8CharLength(lead);
        if (char_len > 1)
        {
            if (i + char_len > text.size())
                char_len = 1;
            else
            {
                for (std::size_t j = 1; j < char_len; ++j)
                {
                    if (!isValidContinuation(static_cast<unsigned char>(text[i + j])))
                    {
                        char_len = 1;
                        break;
                    }
                }
            }
        }
        slices.emplace_back(i, char_len);
        i += char_len;
    }

    if (slices.size() <= 20)
        return text;

    std::string head;
    std::string tail;
    head.reserve(10 * 4);
    tail.reserve(10 * 4);

    for (std::size_t idx = 0; idx < 10 && idx < slices.size(); ++idx)
    {
        const auto [offset, len] = slices[idx];
        head.append(text, offset, len);
    }

    for (std::size_t idx = slices.size() - 10; idx < slices.size(); ++idx)
    {
        const auto [offset, len] = slices[idx];
        tail.append(text, offset, len);
    }

    std::string middle = std::to_string(slices.size());
    std::string result;
    result.reserve(head.size() + middle.size() + tail.size());
    result += head;
    result += middle;
    result += tail;
    return result;
}

std::string YoudaoTranslator::makeSalt()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    static thread_local std::mt19937_64 rng(static_cast<std::uint64_t>(now));
    std::uniform_int_distribution<std::uint64_t> dist;
    std::uint64_t value = dist(rng);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << value;
    return oss.str();
}

std::string YoudaoTranslator::makeCurtime()
{
    auto sec = static_cast<std::uint64_t>(std::time(nullptr));
    return std::to_string(sec);
}

std::string YoudaoTranslator::buildSignature(const std::string& app_key, const std::string& app_secret, const std::string& input, const std::string& salt, const std::string& curtime)
{
    return sha256Hex(app_key + input + salt + curtime + app_secret);
}

std::string YoudaoTranslator::sha256Hex(const std::string& data)
{
    Sha256Ctx ctx;
    sha256_update(ctx, reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    std::uint8_t hash[32];
    sha256_final(ctx, hash);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t b : hash)
        oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

std::string YoudaoTranslator::trimCopy(const std::string& value)
{
    std::string_view view(value);
    while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())))
        view.remove_prefix(1);
    while (!view.empty() && std::isspace(static_cast<unsigned char>(view.back())))
        view.remove_suffix(1);
    if (view.empty())
        return {};
    return std::string(view);
}

std::string YoudaoTranslator::unescapeJson(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    bool escape = false;
    for (char ch : value)
    {
        if (escape)
        {
            switch (ch)
            {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            default: out.push_back(ch); break;
            }
            escape = false;
        }
        else if (ch == '\\')
        {
            escape = true;
        }
        else
        {
            out.push_back(ch);
        }
    }
    return out;
}

bool YoudaoTranslator::extractJsonString(const std::string& json, const std::string& key, std::string& out_value)
{
    const std::string pattern = "\"" + key + "\"";
    std::size_t pos = json.find(pattern);
    if (pos == std::string::npos)
        return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        ++pos;
    if (pos >= json.size())
        return false;
    if (json.compare(pos, 4, "null") == 0)
    {
        out_value.clear();
        return true;
    }
    if (json[pos] != '"')
        return false;
    ++pos;
    std::string raw;
    bool escape = false;
    while (pos < json.size())
    {
        char ch = json[pos++];
        if (escape)
        {
            raw.push_back(ch);
            escape = false;
        }
        else if (ch == '\\')
        {
            escape = true;
        }
        else if (ch == '"')
        {
            break;
        }
        else
        {
            raw.push_back(ch);
        }
    }
    out_value = unescapeJson(raw);
    return true;
}

std::string YoudaoTranslator::testConnection()
{
    if (cfg_.api_key.empty() || cfg_.api_secret.empty())
        return "Error: Missing Youdao credentials";

    std::string result;
    if (mode_ == Mode::LargeModel)
    {
        std::string dst = cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang;
        if (!doLargeModelRequest("Hello", "en", dst, result))
        {
            if (last_error_.empty())
                return "Error: Large model test failed";
            return "Error: Large model test failed - " + last_error_;
        }
        if (result.empty())
            return "Error: Large model test returned empty result";
        return "Success: Youdao large model connection test passed";
    }

    std::string dst = cfg_.target_lang.empty() ? "zh-cn" : cfg_.target_lang;
    if (!doTextRequest("Hello", "en", dst, result))
    {
        if (last_error_.empty())
            return "Error: Text translation test failed";
        return "Error: Text translation test failed - " + last_error_;
    }
    if (result.empty())
        return "Error: Text translation test returned empty result";
    return "Success: Youdao text translation connection test passed";
}
