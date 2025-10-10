#include <catch2/catch_test_macros.hpp>
#include "translate/LabelProcessor.hpp"

TEST_CASE("LabelProcessor replaces <br> with newline and formats select blocks", "[label_processor]")
{
    LabelProcessor lp;
    std::string input = "Hello<br>World<select_nc>\n Option A \nOption B\n<select_end>";
    std::string out = lp.processText(input);

    REQUIRE(out.find("Hello\nWorld") != std::string::npos);
    REQUIRE(out.find("• Option A") != std::string::npos);
    REQUIRE(out.find("• Option B") != std::string::npos);
}

TEST_CASE("LabelProcessor removes ignored labels like <speed=> and <attr> blocks", "[label_processor]")
{
    LabelProcessor lp;
    std::string input = "Start<speed=10>Mid<attr>should be removed<end_attr>End";
    std::string out = lp.processText(input);

    REQUIRE(out.find("<speed=") == std::string::npos);
    REQUIRE(out.find("<attr>") == std::string::npos);
    REQUIRE(out.find("should be removed") == std::string::npos);
    REQUIRE(out.find("Start") != std::string::npos);
    REQUIRE(out.find("End") != std::string::npos);
}

TEST_CASE("LabelProcessor tracks unknown labels and removes them from output", "[label_processor]")
{
    LabelProcessor lp;
    std::string input = "Hello <unknown_label> world <another_label>";
    std::string out = lp.processText(input);

    auto labels = lp.getUnknownLabels();
    REQUIRE(labels.count("<unknown_label>") == 1);
    REQUIRE(labels.count("<another_label>") == 1);

    // Ensure labels removed from output
    REQUIRE(out.find("<unknown_label>") == std::string::npos);
    REQUIRE(out.find("<another_label>") == std::string::npos);
    REQUIRE(out.find("Hello") != std::string::npos);
    REQUIRE(out.find("world") != std::string::npos);
}
