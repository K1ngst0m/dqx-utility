#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace translate
{

struct Header
{
    std::string name;
    std::string value;
};

struct SessionConfig
{
    int connect_timeout_ms = 5000;
    int timeout_ms = 45000;
    std::atomic<bool>* cancel_flag = nullptr;

    // Optional adaptive timeout based on text length
    bool use_adaptive_timeout = true;
    std::size_t text_length_hint = 0; // Set this for adaptive timeout calculation
};

struct HttpResponse
{
    int status_code = 0;
    std::string text;
    std::string error; // non-empty on network/transport errors

    bool ok() const { return error.empty() && status_code >= 200 && status_code < 300; }
};

// JSON POST helper
HttpResponse post_json(const std::string& url, const std::string& body, const std::vector<Header>& headers,
                       const SessionConfig& cfg);

// x-www-form-urlencoded POST helper
HttpResponse post_form(const std::string& url, const std::vector<std::pair<std::string, std::string>>& fields,
                       const SessionConfig& cfg, const std::vector<Header>& headers = {});

// Simple GET helper
HttpResponse get(const std::string& url, const std::vector<Header>& headers, const SessionConfig& cfg);

} // namespace translate
