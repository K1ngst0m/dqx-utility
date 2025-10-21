#pragma once
#include <string>
#include <cstddef>

namespace translate
{
namespace helpers
{

// Backend-specific text length limits (in bytes)
struct LengthLimits
{
    static constexpr std::size_t GOOGLE_FREE_API_MAX = 500; // URL length limit
    static constexpr std::size_t GOOGLE_PAID_API_MAX = 10000; // Reasonable limit
    static constexpr std::size_t OPENAI_API_MAX = 15000; // Conservative limit
    static constexpr std::size_t NIUTRANS_API_MAX = 5000;
    static constexpr std::size_t YOUDAO_API_MAX = 5000;
    static constexpr std::size_t ZHIPU_GLM_API_MAX = 15000;
    static constexpr std::size_t QWEN_MT_API_MAX = 15000;
};

// Calculate adaptive timeout based on text length
// base_timeout_ms: minimum timeout
// text_length: byte count
// Returns: timeout in milliseconds
inline int calculate_adaptive_timeout(int base_timeout_ms, std::size_t text_length)
{
    // Add 2 seconds per 100 characters, with safety factor
    const int extra_ms = static_cast<int>((text_length / 100) * 2000);
    return base_timeout_ms + extra_ms;
}

// Check if text length is within limits
struct LengthCheckResult
{
    bool ok;
    std::string error_message;
    std::size_t text_length;
    std::size_t byte_size;
};

inline LengthCheckResult check_text_length(const std::string& text, std::size_t max_length, const char* backend_name)
{
    LengthCheckResult result;
    result.text_length = text.size();
    result.byte_size = text.length();

    if (text.empty())
    {
        result.ok = false;
        result.error_message = "Empty text";
        return result;
    }

    if (result.byte_size > max_length)
    {
        result.ok = false;
        result.error_message = std::string(backend_name) + " text too long: " + std::to_string(result.byte_size) +
                               " bytes (limit: " + std::to_string(max_length) + " bytes). " +
                               "Consider splitting into smaller chunks.";
        return result;
    }

    result.ok = true;
    return result;
}

// Categorize HTTP errors
enum class HttpErrorType
{
    Success,
    Timeout,
    PayloadTooLarge,
    UriTooLong,
    NetworkError,
    ServerError,
    ClientError,
    Other
};

inline HttpErrorType categorize_http_error(int status_code, const std::string& error_msg)
{
    if (!error_msg.empty())
    {
        // Network/transport errors
        if (error_msg.find("timeout") != std::string::npos || error_msg.find("Timeout") != std::string::npos)
        {
            return HttpErrorType::Timeout;
        }
        return HttpErrorType::NetworkError;
    }

    if (status_code >= 200 && status_code < 300)
    {
        return HttpErrorType::Success;
    }

    switch (status_code)
    {
    case 408: // Request Timeout
    case 504: // Gateway Timeout
        return HttpErrorType::Timeout;
    case 413: // Payload Too Large
        return HttpErrorType::PayloadTooLarge;
    case 414: // URI Too Long
        return HttpErrorType::UriTooLong;
    case 400:
    case 401:
    case 403:
    case 404:
        return HttpErrorType::ClientError;
    default:
        if (status_code >= 500)
            return HttpErrorType::ServerError;
        return HttpErrorType::Other;
    }
}

inline std::string get_error_description(HttpErrorType type, int status_code, const std::string& text_snippet)
{
    switch (type)
    {
    case HttpErrorType::Timeout:
        return "Request timeout - text may be too long to process in time. "
               "Try shorter text or increase timeout.";
    case HttpErrorType::PayloadTooLarge:
        return "HTTP 413 Payload Too Large - text exceeds API limits. "
               "Try splitting into smaller chunks.";
    case HttpErrorType::UriTooLong:
        return "HTTP 414 URI Too Long - text too long for URL. "
               "This backend cannot handle text this long.";
    case HttpErrorType::NetworkError:
        return "Network error: " + text_snippet;
    case HttpErrorType::ServerError:
        return "Server error (HTTP " + std::to_string(status_code) + "): " + text_snippet;
    case HttpErrorType::ClientError:
        return "Client error (HTTP " + std::to_string(status_code) + "): " + text_snippet;
    default:
        return "HTTP " + std::to_string(status_code) + ": " + text_snippet;
    }
}

// Calculate safe buffer size for JSON body
inline std::size_t calculate_json_buffer_size(std::size_t text_length)
{
    // Base overhead for JSON structure (model, messages, etc.)
    const std::size_t base_overhead = 1024;

    // Text can expand up to 2x with JSON escaping (\n, \r, \t, ", \)
    const std::size_t text_overhead = text_length * 2;

    return base_overhead + text_overhead;
}

} // namespace helpers
} // namespace translate
