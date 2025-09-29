#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>
#include "translate/GoogleTranslator.hpp"
#include "../utils/mock_http.hpp"

using namespace translate;

TEST_CASE("Google Translator", "[translate][google]") {
    
    SECTION("Initialization") {
        GoogleTranslator translator;
        
        SECTION("Succeeds with empty config (free tier)") {
            TranslatorConfig config;
            config.backend = Backend::Google;
            config.api_key = ""; // Empty for free tier
            config.target_lang = "zh-cn";
            
            REQUIRE(translator.init(config));
            REQUIRE(translator.isReady());
            translator.shutdown();
        }
        
        SECTION("Succeeds with API key (paid tier)") {
            TranslatorConfig config;
            config.backend = Backend::Google;
            config.api_key = "test-api-key";
            config.target_lang = "en-us";
            
            REQUIRE(translator.init(config));
            REQUIRE(translator.isReady());
            translator.shutdown();
        }
        
        SECTION("Not ready without initialization") {
            REQUIRE_FALSE(translator.isReady());
        }
    }
    
    
    SECTION("Translation workflow") {
        GoogleTranslator translator;
        TranslatorConfig config;
        config.backend = Backend::Google;
        config.api_key = ""; // Use free tier
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
        
        translator.shutdown();
    }
    
    SECTION("Error handling") {
        GoogleTranslator translator;
        
        SECTION("Returns error when not initialized") {
            std::uint64_t id;
            REQUIRE_FALSE(translator.translate("test", "en", "zh-cn", id));
            REQUIRE(std::string(translator.lastError()) == "translator not ready");
        }
    }
    
    SECTION("API fallback behavior") {
        // This would require mocking HTTP responses to fully test
        // For now, we test the basic initialization with different API key states
        
        SECTION("With API key - should try paid API first") {
            GoogleTranslator translator;
            TranslatorConfig config;
            config.backend = Backend::Google;
            config.api_key = "valid-api-key";
            config.target_lang = "zh-cn";
            
            REQUIRE(translator.init(config));
            translator.shutdown();
        }
        
        SECTION("Without API key - should use free API") {
            GoogleTranslator translator;
            TranslatorConfig config;
            config.backend = Backend::Google;
            config.api_key = "";
            config.target_lang = "zh-cn";
            
            REQUIRE(translator.init(config));
            translator.shutdown();
        }
    }
}