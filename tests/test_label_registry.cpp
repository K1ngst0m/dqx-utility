#include <catch2/catch_test_macros.hpp>
#include "processing/LabelRegistry.hpp"
#include "processing/LabelProcessor.hpp"

using namespace label_processing;

TEST_CASE("LabelRegistry - Literal matches", "[label][registry]") {
    LabelRegistry registry;
    
    SECTION("Simple literal labels") {
        auto* br_def = registry.findMatch("<br>");
        REQUIRE(br_def != nullptr);
        REQUIRE(br_def->action == LabelAction::Transform);
        REQUIRE(br_def->replacement == "\n");
        
        auto* close_def = registry.findMatch("<close>");
        REQUIRE(close_def != nullptr);
        REQUIRE(close_def->action == LabelAction::Remove);
    }
    
    SECTION("Case insensitive matching") {
        auto* br_upper = registry.findMatch("<BR>");
        REQUIRE(br_upper != nullptr);
        
        auto* br_mixed = registry.findMatch("<Br>");
        REQUIRE(br_mixed != nullptr);
    }
}

TEST_CASE("LabelRegistry - Wildcard matches", "[label][registry]") {
    LabelRegistry registry;
    
    SECTION("Select with numeric parameters") {
        auto* select1 = registry.findMatch("<select 1>");
        REQUIRE(select1 != nullptr);
        REQUIRE(select1->match_type == LabelMatchType::Paired);
        
        auto* select2 = registry.findMatch("<select 2>");
        REQUIRE(select2 != nullptr);
        
        auto* select99 = registry.findMatch("<select 99>");
        REQUIRE(select99 != nullptr);
    }
    
    SECTION("Speed with parameters") {
        auto* speed0 = registry.findMatch("<speed=0>");
        REQUIRE(speed0 != nullptr);
        REQUIRE(speed0->action == LabelAction::Remove);
        
        auto* speed100 = registry.findMatch("<speed=100>");
        REQUIRE(speed100 != nullptr);
    }
    
    SECTION("YesNo with parameters") {
        auto* yesno2 = registry.findMatch("<yesno 2>");
        REQUIRE(yesno2 != nullptr);
        REQUIRE(yesno2->action == LabelAction::Remove);
    }
    
    SECTION("Case labels with numbers") {
        auto* case1 = registry.findMatch("<case 1>");
        REQUIRE(case1 != nullptr);
        
        auto* case6 = registry.findMatch("<case 6>");
        REQUIRE(case6 != nullptr);
    }
    
    SECTION("Sound effect labels with complex parameters") {
        auto* se_nots = registry.findMatch("<se_nots System 7>");
        REQUIRE(se_nots != nullptr);
        REQUIRE(se_nots->action == LabelAction::Remove);
    }
    
    SECTION("Select_se_off with parameters") {
        auto* select_se_off2 = registry.findMatch("<select_se_off 2>");
        REQUIRE(select_se_off2 != nullptr);
        REQUIRE(select_se_off2->match_type == LabelMatchType::Paired);
    }
}

TEST_CASE("LabelRegistry - Paired label definitions", "[label][registry]") {
    LabelRegistry registry;
    
    SECTION("Select paired labels") {
        auto* select = registry.findMatch("<select>");
        REQUIRE(select != nullptr);
        REQUIRE(select->match_type == LabelMatchType::Paired);
        REQUIRE(select->pair_close == "<select_end>");
        REQUIRE(select->processor != nullptr);
    }
    
    SECTION("Attr paired labels") {
        auto* attr = registry.findMatch("<attr>");
        REQUIRE(attr != nullptr);
        REQUIRE(attr->match_type == LabelMatchType::Paired);
        REQUIRE(attr->pair_close == "<end_attr>");
    }
}

TEST_CASE("LabelProcessor - End-to-end processing", "[label][processor]") {
    LabelProcessor processor;
    
    SECTION("Transform <br> to newline") {
        std::string input = "Line 1<br>Line 2<br>Line 3";
        std::string result = processor.processText(input);
        REQUIRE(result == "Line 1\nLine 2\nLine 3");
    }
    
    SECTION("Remove speed labels") {
        std::string input = "<speed=0>Text here";
        std::string result = processor.processText(input);
        REQUIRE(result == "Text here");
    }
    
    SECTION("Remove yesno labels") {
        std::string input = "要去弃？<yesno 2><close>";
        std::string result = processor.processText(input);
        REQUIRE(result == "要去弃？");
    }
    
    SECTION("Remove case labels") {
        std::string input = "<case 1>Option 1<case 2>Option 2<case_end>";
        std::string result = processor.processText(input);
        REQUIRE(result == "Option 1Option 2");
    }
    
    SECTION("Remove se_nots labels") {
        std::string input = "Got item!<se_nots System 7>";
        std::string result = processor.processText(input);
        REQUIRE(result == "Got item!");
    }
}

TEST_CASE("LabelProcessor - Paired label processing", "[label][processor]") {
    LabelProcessor processor;
    
    SECTION("Basic select block with bullets") {
        std::string input = "<select>\nOption A\nOption B\nOption C\n<select_end>";
        std::string result = processor.processText(input);
        REQUIRE(result.find("• Option A") != std::string::npos);
        REQUIRE(result.find("• Option B") != std::string::npos);
        REQUIRE(result.find("• Option C") != std::string::npos);
    }
    
    SECTION("Select with numeric parameter") {
        std::string input = "Question?<select 1>\nAnswer 1\nAnswer 2\n<select_end>";
        std::string result = processor.processText(input);
        REQUIRE(result.find("Question?") != std::string::npos);
        REQUIRE(result.find("• Answer 1") != std::string::npos);
        REQUIRE(result.find("• Answer 2") != std::string::npos);
    }
    
    SECTION("Select_se_off with parameter") {
        std::string input = "<select_se_off 2>\nItem A\nItem B\n<select_end>";
        std::string result = processor.processText(input);
        REQUIRE(result.find("• Item A") != std::string::npos);
        REQUIRE(result.find("• Item B") != std::string::npos);
    }
    
    SECTION("Attr block removal") {
        std::string input = "<attr><feel_normal_one><end_attr>Text content";
        std::string result = processor.processText(input);
        REQUIRE(result == "Text content");
    }
    
    SECTION("Multiple paired labels") {
        std::string input = "<attr><test><end_attr>Before<br>After<select_nc>\nOpt1\nOpt2\n<select_end>";
        std::string result = processor.processText(input);
        REQUIRE(result.find("Before\nAfter") != std::string::npos);
        REQUIRE(result.find("• Opt1") != std::string::npos);
    }
}

TEST_CASE("LabelProcessor - Real log examples", "[label][processor][integration]") {
    LabelProcessor processor;
    
    SECTION("Example 1: Dialog with select and attr") {
        std::string input = "<attr><feel_normal_one><end_attr><turn_pc>「フリン様。<br>「遺跡の説明<select>\n地下探索\n装備収集\n<select_end>";
        std::string result = processor.processText(input);
        
        // Should remove <attr> block, <turn_pc>, transform <br>
        REQUIRE(result.find("<attr>") == std::string::npos);
        REQUIRE(result.find("<turn_pc>") == std::string::npos);
        REQUIRE(result.find("「フリン様。\n「遺跡の説明") != std::string::npos);
        REQUIRE(result.find("• 地下探索") != std::string::npos);
        REQUIRE(result.find("• 装備収集") != std::string::npos);
    }
    
    SECTION("Example 2: Item discard dialog") {
        std::string input = "<speed=0>要去弃アイテム？<yesno 2><close>";
        std::string result = processor.processText(input);
        
        REQUIRE(result == "要去弃アイテム？");
        REQUIRE(result.find("<speed") == std::string::npos);
        REQUIRE(result.find("<yesno") == std::string::npos);
        REQUIRE(result.find("<close>") == std::string::npos);
    }
    
    SECTION("Example 3: Multi-line select with numbered parameter") {
        std::string input = "「他に　何か？<select 3>\n遺跡に入るには\nゼルメアの聖紋\n<select_end><case 1><case 2><case_end>";
        std::string result = processor.processText(input);
        
        REQUIRE(result.find("「他に　何か？") != std::string::npos);
        REQUIRE(result.find("• 遺跡に入るには") != std::string::npos);
        REQUIRE(result.find("• ゼルメアの聖紋") != std::string::npos);
        REQUIRE(result.find("<case") == std::string::npos);
    }
    
    SECTION("Example 4: Reward notification") {
        std::string input = "<pipipi_off>フリンは　せかいじゅの葉を\n５個　手に入れた！<se_nots System 7><end>";
        std::string result = processor.processText(input);
        
        REQUIRE(result == "フリンは　せかいじゅの葉を\n５個　手に入れた！\n");
        REQUIRE(result.find("<pipipi_off>") == std::string::npos);
        REQUIRE(result.find("<se_nots") == std::string::npos);
    }
}

TEST_CASE("LabelProcessor - Unknown label tracking", "[label][processor]") {
    LabelProcessor processor;
    
    SECTION("Track unknown labels") {
        std::string input = "Text <unknown_label> more text <another_unknown>";
        std::string result = processor.processText(input);
        
        // Unknown labels should be removed
        REQUIRE(result == "Text  more text ");
        
        // Should be tracked
        auto& unknowns = processor.getUnknownLabels();
        REQUIRE(unknowns.count("<unknown_label>") > 0);
        REQUIRE(unknowns.count("<another_unknown>") > 0);
    }
    
    SECTION("Don't track known labels") {
        std::string input = "Text <br> <close> <speed=5>";
        processor.processText(input);
        
        auto& unknowns = processor.getUnknownLabels();
        REQUIRE(unknowns.count("<br>") == 0);
        REQUIRE(unknowns.count("<close>") == 0);
        REQUIRE(unknowns.count("<speed=5>") == 0);
    }
}

TEST_CASE("LabelProcessor - No labels pass to translation", "[label][processor][critical]") {
    LabelProcessor processor;
    
    SECTION("All known labels are removed or transformed") {
        std::string input = "<pipipi_off><speed=0><attr><test><end_attr>Text<br>More<yesno 2><se_nots System 7><close><end>";
        std::string result = processor.processText(input);
        
        // Result should not contain any < or > characters (all labels removed/transformed)
        REQUIRE(result == "Text\nMore");
    }
    
    SECTION("Complex real-world scenario") {
        std::string input = "<attr><feel_normal_one><end_attr><turn_pc>「説明<br><select 2>\nOption 1\nOption 2\n<select_end><case 1><case 2><case_cancel><case_end><break>";
        std::string result = processor.processText(input);
        
        // Verify no angle brackets remain
        REQUIRE(result.find('<') == std::string::npos);
        REQUIRE(result.find('>') == std::string::npos);
        
        // Verify content is preserved
        REQUIRE(result.find("「説明") != std::string::npos);
        REQUIRE(result.find("• Option 1") != std::string::npos);
    }
    
    SECTION("Edge case: Unpaired select_end") {
        // This shouldn't happen but registry should handle it
        std::string input = "Text<select_end>More text";
        std::string result = processor.processText(input);
        
        // select_end should be removed as standalone
        REQUIRE(result == "TextMore text");
    }
}