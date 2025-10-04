#include <catch2/catch_test_macros.hpp>
#include "dqxclarity/pattern/Pattern.hpp"
#include "dqxclarity/pattern/MemoryRegion.hpp"
#include "dqxclarity/pattern/PatternScanner.hpp"
#include "dqxclarity/signatures/Signatures.hpp"
#include "dqxclarity/memory/MemoryFactory.hpp"
#include <unistd.h>

using namespace dqxclarity;

TEST_CASE("Pattern - FromString parsing", "[pattern][parse]") {
    SECTION("Simple hex pattern") {
        auto pattern = Pattern::FromString("FF C7 45");
        REQUIRE(pattern.Size() == 3);
        REQUIRE(pattern.bytes[0] == 0xFF);
        REQUIRE(pattern.bytes[1] == 0xC7);
        REQUIRE(pattern.bytes[2] == 0x45);
        REQUIRE(pattern.mask[0] == true);
        REQUIRE(pattern.mask[1] == true);
        REQUIRE(pattern.mask[2] == true);
    }

    SECTION("Pattern with wildcards") {
        auto pattern = Pattern::FromString("FF ?? C7 45");
        REQUIRE(pattern.Size() == 4);
        REQUIRE(pattern.bytes[0] == 0xFF);
        REQUIRE(pattern.mask[0] == true);
        REQUIRE(pattern.mask[1] == false);
        REQUIRE(pattern.bytes[2] == 0xC7);
        REQUIRE(pattern.mask[2] == true);
    }

    SECTION("Pattern with dot wildcards") {
        auto pattern = Pattern::FromString("FF . C7");
        REQUIRE(pattern.Size() == 3);
        REQUIRE(pattern.mask[0] == true);
        REQUIRE(pattern.mask[1] == false);
        REQUIRE(pattern.mask[2] == true);
    }

    SECTION("Complex pattern from signatures") {
        auto pattern = Pattern::FromString("FF ?? ?? C7 45 ?? 00 00 00 00");
        REQUIRE(pattern.Size() == 10);
        REQUIRE(pattern.IsValid());
    }
}

TEST_CASE("Pattern - FromBytes parsing", "[pattern][bytes]") {
    uint8_t data[] = {0x55, 0x48, 0x89, 0xE5};
    auto pattern = Pattern::FromBytes(data, 4);

    REQUIRE(pattern.Size() == 4);
    REQUIRE(pattern.bytes[0] == 0x55);
    REQUIRE(pattern.bytes[1] == 0x48);
    REQUIRE(pattern.bytes[2] == 0x89);
    REQUIRE(pattern.bytes[3] == 0xE5);

    for (size_t i = 0; i < pattern.Size(); ++i) {
        REQUIRE(pattern.mask[i] == true);
    }
}

TEST_CASE("MemoryRegionParser - Parse /proc/self/maps", "[region][parse]") {
    auto regions = MemoryRegionParser::ParseMaps(getpid());

    REQUIRE(!regions.empty());

    SECTION("Has executable regions") {
        bool has_executable = false;
        for (const auto& region : regions) {
            if (region.IsExecutable()) {
                has_executable = true;
                REQUIRE(region.start < region.end);
                REQUIRE(region.Size() > 0);
                break;
            }
        }
        REQUIRE(has_executable);
    }

    SECTION("Has readable regions") {
        bool has_readable = false;
        for (const auto& region : regions) {
            if (region.IsReadable()) {
                has_readable = true;
                break;
            }
        }
        REQUIRE(has_readable);
    }
}

TEST_CASE("MemoryRegionParser - Filtered parsing", "[region][filter]") {
    SECTION("Readable + Executable") {
        auto regions = MemoryRegionParser::ParseMapsFiltered(getpid(), true, true);
        REQUIRE(!regions.empty());

        for (const auto& region : regions) {
            REQUIRE(region.IsReadable());
            REQUIRE(region.IsExecutable());
        }
    }

    SECTION("Readable only") {
        auto regions = MemoryRegionParser::ParseMapsFiltered(getpid(), true, false);
        REQUIRE(!regions.empty());

        for (const auto& region : regions) {
            REQUIRE(region.IsReadable());
        }
    }
}

TEST_CASE("PatternScanner - Self process scan", "[scanner][self]") {
    auto memory = std::shared_ptr<IProcessMemory>(MemoryFactory::CreatePlatformMemory());
    REQUIRE(memory->AttachProcess(getpid()));

    PatternScanner scanner(memory);

    SECTION("Scan for x64 function prologue") {
        auto pattern = Pattern::FromString("55 48 89 E5");
        auto result = scanner.ScanProcess(pattern, true);

        if (result) {
            INFO("Found pattern at: 0x" << std::hex << *result);
            REQUIRE(*result > 0);
        }
    }

    SECTION("Scan for pattern that doesn't exist") {
        auto pattern = Pattern::FromString("DE AD BE EF CA FE BA BE");
        auto result = scanner.ScanProcess(pattern, true);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("Signatures - DQX patterns initialization", "[signatures]") {
    SECTION("Dialog trigger signature") {
        const auto& pattern = Signatures::GetDialogTrigger();
        REQUIRE(pattern.IsValid());
        REQUIRE(pattern.Size() > 0);
    }

    SECTION("All signatures valid") {
        const auto& dialog = Signatures::GetDialogTrigger();
        const auto& integrity = Signatures::GetIntegrityCheck();
        const auto& network = Signatures::GetNetworkText();
        const auto& quest = Signatures::GetQuestText();
        const auto& corner = Signatures::GetCornerText();

        REQUIRE(dialog.IsValid());
        REQUIRE(integrity.IsValid());
        REQUIRE(network.IsValid());
        REQUIRE(quest.IsValid());
        REQUIRE(corner.IsValid());
    }

    SECTION("Lookup by name") {
        auto pattern = Signatures::GetSignature("dialog_trigger");
        REQUIRE(pattern != nullptr);
        REQUIRE(pattern->IsValid());

        auto invalid = Signatures::GetSignature("nonexistent");
        REQUIRE(invalid == nullptr);
    }
}

TEST_CASE("PatternScanner - Multiple matches", "[scanner][all]") {
    auto memory = std::shared_ptr<IProcessMemory>(MemoryFactory::CreatePlatformMemory());
    REQUIRE(memory->AttachProcess(getpid()));

    PatternScanner scanner(memory);

    SECTION("Find all matches for common byte") {
        auto pattern = Pattern::FromString("00 00");
        auto results = scanner.ScanProcessAll(pattern, false);

        REQUIRE(!results.empty());
    }
}
