#include <catch2/catch_test_macros.hpp>
#include "translate/ITranslator.hpp"
#include "translate/OpenAITranslator.hpp"
#include "translate/GoogleTranslator.hpp"

TEST_CASE("Translator Factory", "[translate][factory]") {
    
    SECTION("Creates OpenAI translator") {
        auto translator = translate::createTranslator(translate::Backend::OpenAI);
        REQUIRE(translator != nullptr);
        
        // Verify it's the correct type by testing OpenAI-specific behavior
        translate::TranslatorConfig config;
        config.backend = translate::Backend::OpenAI;
        config.api_key = "test-key";
        config.base_url = "https://api.openai.com";
        config.model = "gpt-3.5-turbo";
        config.target_lang = "en-us";
        
        // Should initialize successfully with valid config
        REQUIRE(translator->init(config));
        translator->shutdown();
    }
    
    SECTION("Creates Google translator") {
        auto translator = translate::createTranslator(translate::Backend::Google);
        REQUIRE(translator != nullptr);
        
        // Verify it's the correct type by testing Google-specific behavior  
        translate::TranslatorConfig config;
        config.backend = translate::Backend::Google;
        config.api_key = ""; // Google supports empty API key for free tier
        config.target_lang = "zh-cn";
        
        // Should initialize successfully even without API key
        REQUIRE(translator->init(config));
        translator->shutdown();
    }
    
    SECTION("Backend enum values are correct") {
        REQUIRE(static_cast<int>(translate::Backend::OpenAI) == 0);
        REQUIRE(static_cast<int>(translate::Backend::Google) == 1);
    }
}