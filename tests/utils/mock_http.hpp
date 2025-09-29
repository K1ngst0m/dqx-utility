#pragma once

#include <string>
#include <unordered_map>
#include <functional>

namespace test_utils {

// Mock HTTP response structure
struct MockResponse {
    int status_code = 200;
    std::string body;
    std::string error_message;
    bool has_error = false;
};

// Mock HTTP client for testing
class MockHttpClient {
public:
    // Set response for a specific URL
    void setResponse(const std::string& url, const MockResponse& response);
    
    // Set response based on URL pattern matching
    void setPatternResponse(const std::string& pattern, const MockResponse& response);
    
    // Simulate a network error for all requests
    void simulateNetworkError(const std::string& error_msg);
    
    // Clear all mocked responses
    void clearResponses();
    
    // Get mocked response for URL
    MockResponse getResponse(const std::string& url) const;
    
private:
    std::unordered_map<std::string, MockResponse> url_responses_;
    std::unordered_map<std::string, MockResponse> pattern_responses_;
    bool simulate_error_ = false;
    std::string error_message_;
};

// Common mock responses for testing
class MockResponses {
public:
    // OpenAI API responses
    static MockResponse openai_success(const std::string& translated_text);
    static MockResponse openai_error_401();
    static MockResponse openai_error_quota();
    static MockResponse openai_invalid_json();
    
    // Google Translate API responses
    static MockResponse google_paid_success(const std::string& translated_text);
    static MockResponse google_free_success(const std::string& translated_text);
    static MockResponse google_error_403();
    static MockResponse google_error_quota();
    static MockResponse google_invalid_json();
    
    // Generic error responses
    static MockResponse network_error();
    static MockResponse timeout_error();
};

}  // namespace test_utils