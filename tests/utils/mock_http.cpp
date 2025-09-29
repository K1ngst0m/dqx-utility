#include "mock_http.hpp"
#include <regex>

namespace test_utils {

void MockHttpClient::setResponse(const std::string& url, const MockResponse& response) {
    url_responses_[url] = response;
}

void MockHttpClient::setPatternResponse(const std::string& pattern, const MockResponse& response) {
    pattern_responses_[pattern] = response;
}

void MockHttpClient::simulateNetworkError(const std::string& error_msg) {
    simulate_error_ = true;
    error_message_ = error_msg;
}

void MockHttpClient::clearResponses() {
    url_responses_.clear();
    pattern_responses_.clear();
    simulate_error_ = false;
    error_message_.clear();
}

MockResponse MockHttpClient::getResponse(const std::string& url) const {
    if (simulate_error_) {
        MockResponse error_resp;
        error_resp.has_error = true;
        error_resp.error_message = error_message_;
        return error_resp;
    }
    
    // Check exact URL match first
    auto url_it = url_responses_.find(url);
    if (url_it != url_responses_.end()) {
        return url_it->second;
    }
    
    // Check pattern matches
    for (const auto& [pattern, response] : pattern_responses_) {
        try {
            std::regex pattern_regex(pattern);
            if (std::regex_search(url, pattern_regex)) {
                return response;
            }
        } catch (const std::exception&) {
            // Invalid regex pattern, skip
            continue;
        }
    }
    
    // Default 404 response
    MockResponse not_found;
    not_found.status_code = 404;
    not_found.body = "Not Found";
    return not_found;
}

// OpenAI API responses
MockResponse MockResponses::openai_success(const std::string& translated_text) {
    MockResponse response;
    response.status_code = 200;
    response.body = R"({
        "choices": [
            {
                "message": {
                    "content": ")" + translated_text + R"("
                }
            }
        ]
    })";
    return response;
}

MockResponse MockResponses::openai_error_401() {
    MockResponse response;
    response.status_code = 401;
    response.body = R"({
        "error": {
            "message": "Invalid API key provided",
            "type": "invalid_request_error"
        }
    })";
    return response;
}

MockResponse MockResponses::openai_error_quota() {
    MockResponse response;
    response.status_code = 429;
    response.body = R"({
        "error": {
            "message": "Rate limit reached",
            "type": "rate_limit_error"
        }
    })";
    return response;
}

MockResponse MockResponses::openai_invalid_json() {
    MockResponse response;
    response.status_code = 200;
    response.body = "invalid json{";
    return response;
}

// Google Translate API responses
MockResponse MockResponses::google_paid_success(const std::string& translated_text) {
    MockResponse response;
    response.status_code = 200;
    response.body = R"({
        "data": {
            "translations": [
                {
                    "translatedText": ")" + translated_text + R"("
                }
            ]
        }
    })";
    return response;
}

MockResponse MockResponses::google_free_success(const std::string& translated_text) {
    MockResponse response;
    response.status_code = 200;
    response.body = R"([[[")" + translated_text + R"(","Hello",null,null,3]],null,"en"])";
    return response;
}

MockResponse MockResponses::google_error_403() {
    MockResponse response;
    response.status_code = 403;
    response.body = R"({
        "error": {
            "code": 403,
            "message": "The request is missing a valid API key."
        }
    })";
    return response;
}

MockResponse MockResponses::google_error_quota() {
    MockResponse response;
    response.status_code = 429;
    response.body = R"({
        "error": {
            "code": 429,
            "message": "Quota exceeded"
        }
    })";
    return response;
}

MockResponse MockResponses::google_invalid_json() {
    MockResponse response;
    response.status_code = 200;
    response.body = "invalid json response}";
    return response;
}

// Generic error responses
MockResponse MockResponses::network_error() {
    MockResponse response;
    response.has_error = true;
    response.error_message = "Network connection failed";
    return response;
}

MockResponse MockResponses::timeout_error() {
    MockResponse response;
    response.has_error = true;
    response.error_message = "Request timeout";
    return response;
}

}  // namespace test_utils