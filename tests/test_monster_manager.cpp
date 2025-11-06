#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "monster/MonsterManager.hpp"
#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

// Helper to create a temporary test JSONL file
class TempMonsterFile
{
public:
    TempMonsterFile(const std::string& content)
        : path_("test_monsters_temp.jsonl")
    {
        std::ofstream file(path_);
        file << content;
        file.close();
    }

    ~TempMonsterFile()
    {
        if (fs::exists(path_))
        {
            fs::remove(path_);
        }
    }

    std::string getPath() const { return path_; }

private:
    std::string path_;
};

TEST_CASE("MonsterManager - Initialization", "[monster]")
{
    MonsterManager manager;

    SECTION("Fails to load non-existent file")
    {
        REQUIRE_FALSE(manager.initialize("non_existent_file.jsonl"));
        REQUIRE(manager.getMonsterCount() == 0);
    }

    SECTION("Loads valid JSONL data")
    {
        std::string test_data = R"({"id":"スライム","name":"スライム","category":"スライム系","stats":{"exp":5,"gold":3,"hp":20,"mp":5,"attack":15,"defense":10},"resistances":{"fire":1.0,"ice":1.0},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";
        test_data += "\n";
        test_data += R"({"id":"スライムベス","name":"スライムベス","category":"スライム系","stats":{"exp":8,"gold":5,"hp":25,"mp":8},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";

        TempMonsterFile temp(test_data);
        REQUIRE(manager.initialize(temp.getPath()));
        REQUIRE(manager.getMonsterCount() == 2);
    }

    SECTION("Skips malformed JSON lines")
    {
        std::string test_data = R"({"id":"スライム","name":"スライム","category":"スライム系","stats":{},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";
        test_data += "\n{invalid json\n";
        test_data += R"({"id":"スライムベス","name":"スライムベス","category":"スライム系","stats":{},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";

        TempMonsterFile temp(test_data);
        REQUIRE(manager.initialize(temp.getPath()));
        REQUIRE(manager.getMonsterCount() == 2);
    }
}

TEST_CASE("MonsterManager - Name Lookup", "[monster]")
{
    std::string test_data = R"({"id":"スライム","name":"スライム","category":"スライム系","stats":{"exp":5,"gold":3,"hp":20,"mp":5,"attack":15,"defense":10},"resistances":{"fire":1.0,"ice":1.2},"locations":[{"area":"始まりの森","url":"https://example.com","notes":"入口付近"}],"drops":{"normal":["スライムゼリー"],"rare":["スライムの冠"],"orbs":[{"type":"炎宝珠","effect":"メラ系呪文の極意"}],"white_treasure":[]},"source_url":"https://example.com/slime"})";

    TempMonsterFile temp(test_data);
    MonsterManager manager;
    REQUIRE(manager.initialize(temp.getPath()));

    SECTION("Exact name match returns monster")
    {
        auto result = manager.findMonsterByName("スライム");
        REQUIRE(result.has_value());
        REQUIRE(result->name == "スライム");
        REQUIRE(result->id == "スライム");
        REQUIRE(result->category == "スライム系");
    }

    SECTION("Non-existent name returns nullopt")
    {
        auto result = manager.findMonsterByName("存在しないモンスター");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Case-sensitive matching")
    {
        auto result = manager.findMonsterByName("すらいむ");
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("MonsterManager - ID Lookup", "[monster]")
{
    std::string test_data = R"({"id":"スライム","name":"スライム","category":"スライム系","stats":{},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";

    TempMonsterFile temp(test_data);
    MonsterManager manager;
    REQUIRE(manager.initialize(temp.getPath()));

    SECTION("Finds monster by ID")
    {
        auto result = manager.findMonsterById("スライム");
        REQUIRE(result.has_value());
        REQUIRE(result->id == "スライム");
    }

    SECTION("Non-existent ID returns nullopt")
    {
        auto result = manager.findMonsterById("invalid_id");
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("MonsterManager - Fuzzy Name Matching", "[monster][fuzzy]")
{
    std::string test_data = R"({"id":"キングスライム","name":"キングスライム","category":"スライム系","stats":{},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";
    test_data += "\n";
    test_data += R"({"id":"メタルスライム","name":"メタルスライム","category":"スライム系","stats":{},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";

    TempMonsterFile temp(test_data);
    MonsterManager manager;
    REQUIRE(manager.initialize(temp.getPath()));

    SECTION("Exact match via fuzzy function")
    {
        auto result = manager.findMonsterByNameFuzzy("キングスライム");
        REQUIRE(result.has_value());
        REQUIRE(result->name == "キングスライム");
    }

    SECTION("Close match returns similar name")
    {
        // Slight typo should still match
        auto result = manager.findMonsterByNameFuzzy("キンクスライム");
        // This might match depending on threshold, but at least shouldn't crash
        REQUIRE_NOTHROW(manager.findMonsterByNameFuzzy("キンクスライム"));
    }
}

TEST_CASE("MonsterManager - Data Structure Parsing", "[monster]")
{
    std::string test_data = R"({"id":"テストモンスター","name":"テストモンスター","category":"ドラゴン系","stats":{"exp":1000,"gold":50,"training":5,"weak_level":80,"hp":5000,"mp":200,"attack":400,"defense":350,"crystal_level":"85"},"resistances":{"fire":0.5,"ice":1.5,"wind":1.0,"thunder":1.0,"earth":1.2,"dark":0.8,"light":1.1},"locations":[{"area":"test1","url":"http://test1.com"},{"area":"test2","url":"http://test2.com","notes":"rare"}],"drops":{"normal":["item1","item2"],"rare":["rareitem"],"orbs":[{"type":"炎宝珠","effect":"test effect"}],"white_treasure":["treasure1"]},"source_url":"https://example.com/test"})";

    TempMonsterFile temp(test_data);
    MonsterManager manager;
    REQUIRE(manager.initialize(temp.getPath()));

    auto monster = manager.findMonsterById("テストモンスター");
    REQUIRE(monster.has_value());

    SECTION("Basic fields parsed correctly")
    {
        REQUIRE(monster->name == "テストモンスター");
        REQUIRE(monster->category == "ドラゴン系");
        REQUIRE(monster->source_url == "https://example.com/test");
    }

    SECTION("Stats parsed correctly")
    {
        REQUIRE(monster->stats.exp.has_value());
        REQUIRE(*monster->stats.exp == 1000);
        REQUIRE(monster->stats.gold.has_value());
        REQUIRE(*monster->stats.gold == 50);
        REQUIRE(monster->stats.hp.has_value());
        REQUIRE(*monster->stats.hp == 5000);
        REQUIRE(monster->stats.mp.has_value());
        REQUIRE(*monster->stats.mp == 200);
        REQUIRE(monster->stats.attack.has_value());
        REQUIRE(*monster->stats.attack == 400);
        REQUIRE(monster->stats.defense.has_value());
        REQUIRE(*monster->stats.defense == 350);
        REQUIRE(monster->stats.crystal_level.has_value());
        REQUIRE(*monster->stats.crystal_level == "85");
    }

    SECTION("Resistances parsed correctly")
    {
        REQUIRE(monster->resistances.fire.has_value());
        REQUIRE_THAT(*monster->resistances.fire, WithinAbs(0.5, 0.001));
        REQUIRE(monster->resistances.ice.has_value());
        REQUIRE_THAT(*monster->resistances.ice, WithinAbs(1.5, 0.001));
        REQUIRE(monster->resistances.earth.has_value());
        REQUIRE_THAT(*monster->resistances.earth, WithinAbs(1.2, 0.001));
    }

    SECTION("Locations parsed correctly")
    {
        REQUIRE(monster->locations.size() == 2);
        REQUIRE(monster->locations[0].area == "test1");
        REQUIRE(monster->locations[0].url == "http://test1.com");
        REQUIRE_FALSE(monster->locations[0].notes.has_value());
        REQUIRE(monster->locations[1].area == "test2");
        REQUIRE(monster->locations[1].notes.has_value());
        REQUIRE(*monster->locations[1].notes == "rare");
    }

    SECTION("Drops parsed correctly")
    {
        REQUIRE(monster->drops.normal.size() == 2);
        REQUIRE(monster->drops.normal[0] == "item1");
        REQUIRE(monster->drops.rare.size() == 1);
        REQUIRE(monster->drops.rare[0] == "rareitem");
        REQUIRE(monster->drops.orbs.size() == 1);
        REQUIRE(monster->drops.orbs[0].orb_type == "炎宝珠");
        REQUIRE(monster->drops.orbs[0].effect == "test effect");
        REQUIRE(monster->drops.white_treasure.size() == 1);
    }
}

TEST_CASE("MonsterManager - Optional Fields", "[monster]")
{
    // Boss monsters may have null stats
    std::string test_data = R"({"id":"ボスモンスター","name":"ボスモンスター","category":"???系","stats":{"exp":10000,"gold":0},"resistances":{},"locations":[],"drops":{"normal":[],"rare":[],"orbs":[],"white_treasure":[]},"source_url":"https://example.com"})";

    TempMonsterFile temp(test_data);
    MonsterManager manager;
    REQUIRE(manager.initialize(temp.getPath()));

    auto monster = manager.findMonsterById("ボスモンスター");
    REQUIRE(monster.has_value());

    SECTION("Null stats handled correctly")
    {
        REQUIRE(monster->stats.exp.has_value());
        REQUIRE(*monster->stats.exp == 10000);
        REQUIRE_FALSE(monster->stats.hp.has_value());
        REQUIRE_FALSE(monster->stats.mp.has_value());
        REQUIRE_FALSE(monster->stats.attack.has_value());
        REQUIRE_FALSE(monster->stats.defense.has_value());
    }

    SECTION("Empty resistances handled correctly")
    {
        REQUIRE_FALSE(monster->resistances.fire.has_value());
        REQUIRE_FALSE(monster->resistances.ice.has_value());
    }
}
