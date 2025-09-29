#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>
#include "translate/OpenAITranslator.hpp"
#include "../utils/mock_http.hpp"

using namespace translate;

TEST_CASE("OpenAI Translator", "[translate][openai]") {
    
    SECTION("Initialization") {
        OpenAITranslator translator;
        
        SECTION("Initializes but not ready with empty config") {
            TranslatorConfig config;
            REQUIRE(translator.init(config)); // init() always succeeds
            REQUIRE_FALSE(translator.isReady()); // but translator is not ready
            translator.shutdown();
        }
        
        SECTION("Succeeds with valid config") {
            TranslatorConfig config;
            config.backend = Backend::OpenAI;
            config.api_key = "test-key";
            config.base_url = "https://api.openai.com";
            config.model = "gpt-3.5-turbo";
            config.target_lang = "en-us";
            
            REQUIRE(translator.init(config));
            REQUIRE(translator.isReady());
            translator.shutdown();
        }
        
        SECTION("Not ready without proper initialization") {
            REQUIRE_FALSE(translator.isReady());
        }
    }
    
    
    SECTION("Translation workflow") {
        OpenAITranslator translator;
        TranslatorConfig config;
        config.backend = Backend::OpenAI;
        config.api_key = "test-key";
        config.base_url = "https://api.openai.com";
        config.model = "gpt-3.5-turbo";
        config.target_lang = "zh-cn";
        
        REQUIRE(translator.init(config));
        
        SECTION("Rejects empty text") {
            std::uint64_t id;
            REQUIRE_FALSE(translator.translate("", "en", "zh-cn", id));
        }
        
        SECTION("Rejects whitespace-only text") {
            std::uint64_t id;
            REQUIRE_FALSE(translator.translate("   \n\t  ", "en", "zh-cn", id));
        }
        
        SECTION("Accepts valid text") {
            std::uint64_t id;
            REQUIRE(translator.translate("Hello, world!", "en", "zh-cn", id));
            REQUIRE(id > 0);
        }
        
        // Note: Real network tests would require mocking the HTTP client
        // For now, we test the interface behavior
        
        translator.shutdown();
    }
    
    SECTION("Error handling") {
        OpenAITranslator translator;
        
        SECTION("Returns error when not initialized") {
            std::uint64_t id;
            REQUIRE_FALSE(translator.translate("test", "en", "zh-cn", id));
            REQUIRE(std::string(translator.lastError()) == "translator not ready");
        }
    }
}