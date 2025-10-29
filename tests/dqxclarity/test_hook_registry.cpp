#include <catch2/catch_test_macros.hpp>
#include <dqxclarity/hooking/HookRegistry.hpp>

#include <chrono>
#include <filesystem>

using namespace dqxclarity::persistence;

TEST_CASE("HookRegistry - Basic operations", "[hook_registry]")
{
    // Ensure clean state
    HookRegistry::ClearRegistry();

    SECTION("CRC32 computation is consistent")
    {
        std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04, 0x05 };
        uint32_t crc1 = HookRegistry::ComputeCRC32(data.data(), data.size());
        uint32_t crc2 = HookRegistry::ComputeCRC32(data.data(), data.size());

        REQUIRE(crc1 == crc2);
        REQUIRE(crc1 != 0);
    }

    SECTION("Empty registry returns no orphans")
    {
        auto orphans = HookRegistry::LoadOrphanedHooks();
        REQUIRE(orphans.empty());
    }

    SECTION("HookTypeToString returns correct names")
    {
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Dialog)) == "DialogHook");
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Quest)) == "QuestHook");
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Player)) == "PlayerHook");
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Network)) == "NetworkHook");
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Corner)) == "CornerHook");
        REQUIRE(std::string(HookRegistry::HookTypeToString(HookType::Integrity)) == "IntegrityHook");
    }

    SECTION("GetRegistryPath returns valid path")
    {
        auto path = HookRegistry::GetRegistryPath();
        REQUIRE(!path.empty());
        REQUIRE(path.filename() == "hook_registry.bin");
    }

    // Cleanup
    HookRegistry::ClearRegistry();
}

TEST_CASE("HookRegistry - Register and load hooks", "[hook_registry]")
{
    HookRegistry::ClearRegistry();

    SECTION("Register single hook")
    {
        HookRecord record;
        record.type = HookType::Dialog;
        record.process_id = 1234;
        record.hook_address = 0x12345678;
        record.detour_address = 0x87654321;
        record.detour_size = 4096;
        record.original_bytes = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
        record.installed_time = std::chrono::system_clock::now();
        record.hook_checksum = HookRegistry::ComputeCRC32(record.original_bytes.data(), record.original_bytes.size());
        record.detour_checksum = 0xDEADBEEF;

        REQUIRE(HookRegistry::RegisterHook(record));

        auto orphans = HookRegistry::LoadOrphanedHooks();
        REQUIRE(orphans.size() == 1);
        REQUIRE(orphans[0].type == HookType::Dialog);
        REQUIRE(orphans[0].process_id == 1234);
        REQUIRE(orphans[0].hook_address == 0x12345678);
        REQUIRE(orphans[0].detour_address == 0x87654321);
        REQUIRE(orphans[0].detour_size == 4096);
        REQUIRE(orphans[0].original_bytes.size() == 5);
        REQUIRE(orphans[0].hook_checksum == record.hook_checksum);
    }

    SECTION("Register multiple hooks")
    {
        HookRecord dialog_hook;
        dialog_hook.type = HookType::Dialog;
        dialog_hook.process_id = 5678;
        dialog_hook.hook_address = 0x11111111;
        dialog_hook.detour_address = 0;
        dialog_hook.detour_size = 0;
        dialog_hook.original_bytes = { 0x90, 0x90, 0x90 };
        dialog_hook.installed_time = std::chrono::system_clock::now();
        dialog_hook.hook_checksum =
            HookRegistry::ComputeCRC32(dialog_hook.original_bytes.data(), dialog_hook.original_bytes.size());
        dialog_hook.detour_checksum = 0;

        HookRecord quest_hook;
        quest_hook.type = HookType::Quest;
        quest_hook.process_id = 5678;
        quest_hook.hook_address = 0x22222222;
        quest_hook.detour_address = 0;
        quest_hook.detour_size = 0;
        quest_hook.original_bytes = { 0xCC, 0xCC };
        quest_hook.installed_time = std::chrono::system_clock::now();
        quest_hook.hook_checksum =
            HookRegistry::ComputeCRC32(quest_hook.original_bytes.data(), quest_hook.original_bytes.size());
        quest_hook.detour_checksum = 0;

        REQUIRE(HookRegistry::RegisterHook(dialog_hook));
        REQUIRE(HookRegistry::RegisterHook(quest_hook));

        auto orphans = HookRegistry::LoadOrphanedHooks();
        REQUIRE(orphans.size() == 2);
    }

    SECTION("Unregister hook")
    {
        HookRecord record;
        record.type = HookType::Player;
        record.process_id = 9999;
        record.hook_address = 0x33333333;
        record.detour_address = 0;
        record.detour_size = 0;
        record.original_bytes = { 0xFF, 0xFF };
        record.installed_time = std::chrono::system_clock::now();
        record.hook_checksum = HookRegistry::ComputeCRC32(record.original_bytes.data(), record.original_bytes.size());
        record.detour_checksum = 0;

        REQUIRE(HookRegistry::RegisterHook(record));
        REQUIRE(HookRegistry::LoadOrphanedHooks().size() == 1);

        REQUIRE(HookRegistry::UnregisterHook(HookType::Player));
        REQUIRE(HookRegistry::LoadOrphanedHooks().empty());
    }

    SECTION("Replace existing hook of same type")
    {
        HookRecord record1;
        record1.type = HookType::Corner;
        record1.process_id = 1111;
        record1.hook_address = 0x44444444;
        record1.detour_address = 0;
        record1.detour_size = 0;
        record1.original_bytes = { 0xAA };
        record1.installed_time = std::chrono::system_clock::now();
        record1.hook_checksum =
            HookRegistry::ComputeCRC32(record1.original_bytes.data(), record1.original_bytes.size());
        record1.detour_checksum = 0;

        HookRecord record2;
        record2.type = HookType::Corner; // Same type
        record2.process_id = 2222; // Different PID
        record2.hook_address = 0x55555555;
        record2.detour_address = 0;
        record2.detour_size = 0;
        record2.original_bytes = { 0xBB, 0xBB };
        record2.installed_time = std::chrono::system_clock::now();
        record2.hook_checksum =
            HookRegistry::ComputeCRC32(record2.original_bytes.data(), record2.original_bytes.size());
        record2.detour_checksum = 0;

        REQUIRE(HookRegistry::RegisterHook(record1));
        REQUIRE(HookRegistry::RegisterHook(record2));

        // Should only have one hook (the second one replaced the first)
        auto orphans = HookRegistry::LoadOrphanedHooks();
        REQUIRE(orphans.size() == 1);
        REQUIRE(orphans[0].process_id == 2222);
        REQUIRE(orphans[0].hook_address == 0x55555555);
    }

    // Cleanup
    HookRegistry::ClearRegistry();
}

TEST_CASE("HookRegistry - ClearRegistry", "[hook_registry]")
{
    HookRegistry::ClearRegistry();

    SECTION("Clearing empty registry succeeds") { REQUIRE(HookRegistry::ClearRegistry()); }

    SECTION("Clearing non-empty registry removes file")
    {
        HookRecord record;
        record.type = HookType::Network;
        record.process_id = 7777;
        record.hook_address = 0x66666666;
        record.detour_address = 0;
        record.detour_size = 0;
        record.original_bytes = { 0xEE };
        record.installed_time = std::chrono::system_clock::now();
        record.hook_checksum = HookRegistry::ComputeCRC32(record.original_bytes.data(), record.original_bytes.size());
        record.detour_checksum = 0;

        REQUIRE(HookRegistry::RegisterHook(record));
        REQUIRE(std::filesystem::exists(HookRegistry::GetRegistryPath()));

        REQUIRE(HookRegistry::ClearRegistry());
        REQUIRE(!std::filesystem::exists(HookRegistry::GetRegistryPath()));
    }

    // Cleanup
    HookRegistry::ClearRegistry();
}

TEST_CASE("HookRegistry - IsProcessAlive", "[hook_registry]")
{
    SECTION("Non-existent PID returns false")
    {
        // Use an extremely high PID that's unlikely to exist
        REQUIRE_FALSE(HookRegistry::IsProcessAlive(999999999));
    }

    // Note: We can't reliably test a positive case without knowing a valid PID,
    // and testing with the current process is platform-specific
}
