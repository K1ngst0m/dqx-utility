#include <catch2/catch_test_macros.hpp>
#include "dqxclarity/memory/MemoryFactory.hpp"
#include "dqxclarity/memory/IProcessMemory.hpp"
#include <unistd.h>
#include <cstring>
#include <memory>

using namespace dqxclarity;

// Helper function to create memory instance for testing
std::unique_ptr<IProcessMemory> CreateTestMemory() {
    return MemoryFactory::CreatePlatformMemory();
}

TEST_CASE("IProcessMemory - Basic Construction", "[memory][basic][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    SECTION("Initial state") {
        REQUIRE_FALSE(memory->IsProcessAttached());
        REQUIRE(memory->GetAttachedPid() == -1);
    }
}

TEST_CASE("IProcessMemory - Invalid Process Attachment", "[memory][attach][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    SECTION("Invalid PID - negative") {
        REQUIRE_FALSE(memory->AttachProcess(-1));
        REQUIRE_FALSE(memory->IsProcessAttached());
    }

    SECTION("Invalid PID - zero") {
        REQUIRE_FALSE(memory->AttachProcess(0));
        REQUIRE_FALSE(memory->IsProcessAttached());
    }

    SECTION("Non-existent PID") {
        REQUIRE_FALSE(memory->AttachProcess(999999));
        REQUIRE_FALSE(memory->IsProcessAttached());
    }
}

TEST_CASE("IProcessMemory - Self Process Attachment", "[memory][attach][self][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    pid_t self_pid = getpid();

    SECTION("Valid self attachment") {
        REQUIRE(memory->AttachProcess(self_pid));
        REQUIRE(memory->IsProcessAttached());
        REQUIRE(memory->GetAttachedPid() == self_pid);

        memory->DetachProcess();
        REQUIRE_FALSE(memory->IsProcessAttached());
        REQUIRE(memory->GetAttachedPid() == -1);
    }
}

TEST_CASE("IProcessMemory - Self Memory Operations", "[memory][operations][self][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    pid_t self_pid = getpid();
    REQUIRE(memory->AttachProcess(self_pid));

    SECTION("Read memory - valid") {
        int test_value = 0x12345678;
        int read_value = 0;

        REQUIRE(memory->ReadMemory(reinterpret_cast<uintptr_t>(&test_value), &read_value, sizeof(int)));
        REQUIRE(read_value == test_value);
    }

    SECTION("Write memory - valid") {
        int write_target = 0;
        int write_value = 0x87654321;

        REQUIRE(memory->WriteMemory(reinterpret_cast<uintptr_t>(&write_target), &write_value, sizeof(int)));
        REQUIRE(write_target == write_value);
    }

    SECTION("Read memory - invalid parameters") {
        int buffer;
        REQUIRE_FALSE(memory->ReadMemory(0, nullptr, sizeof(int)));  // null buffer
        REQUIRE_FALSE(memory->ReadMemory(0, &buffer, 0));           // zero size
    }

    SECTION("Write memory - invalid parameters") {
        int value = 42;
        REQUIRE_FALSE(memory->WriteMemory(0, nullptr, sizeof(int))); // null buffer
        REQUIRE_FALSE(memory->WriteMemory(0, &value, 0));           // zero size
    }

    SECTION("String operations") {
        const char* test_string = "Hello, DQXClarity!";
        char read_buffer[32];
        memset(read_buffer, 0, sizeof(read_buffer));

        REQUIRE(memory->ReadMemory(reinterpret_cast<uintptr_t>(test_string), read_buffer, strlen(test_string) + 1));
        REQUIRE(strcmp(read_buffer, test_string) == 0);
    }

    SECTION("Complex data structures") {
        struct TestStruct {
            int a;
            float b;
            char c[8];
        };

        TestStruct original = {42, 3.14f, "test"};
        TestStruct read_back = {};

        REQUIRE(memory->ReadMemory(reinterpret_cast<uintptr_t>(&original), &read_back, sizeof(TestStruct)));
        REQUIRE(read_back.a == original.a);
        REQUIRE(read_back.b == original.b);
        REQUIRE(strcmp(read_back.c, original.c) == 0);
    }
}

TEST_CASE("IProcessMemory - Operations Without Attachment", "[memory][operations][unattached][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    SECTION("Read without attachment") {
        int buffer;
        REQUIRE_FALSE(memory->ReadMemory(0x1000, &buffer, sizeof(int)));
    }

    SECTION("Write without attachment") {
        int value = 42;
        REQUIRE_FALSE(memory->WriteMemory(0x1000, &value, sizeof(int)));
    }
}

TEST_CASE("IProcessMemory - Multiple Attachments", "[memory][attach][multiple][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    pid_t self_pid = getpid();

    SECTION("Re-attachment to same process") {
        REQUIRE(memory->AttachProcess(self_pid));
        REQUIRE(memory->IsProcessAttached());

        // Re-attach to same process
        REQUIRE(memory->AttachProcess(self_pid));
        REQUIRE(memory->IsProcessAttached());
        REQUIRE(memory->GetAttachedPid() == self_pid);
    }

    SECTION("Attachment to different process after detach") {
        REQUIRE(memory->AttachProcess(self_pid));
        memory->DetachProcess();

        // Try to attach to init process (PID 1) - may fail due to permissions, but should not crash
        memory->AttachProcess(1);
        // Don't assert success as this depends on system permissions
    }
}

TEST_CASE("IProcessMemory - Edge Cases", "[memory][edge][interface]") {
    auto memory = CreateTestMemory();
    REQUIRE(memory != nullptr);

    pid_t self_pid = getpid();
    REQUIRE(memory->AttachProcess(self_pid));

    SECTION("Large memory operations") {
        constexpr size_t LARGE_SIZE = 1024;
        std::vector<char> original(LARGE_SIZE);
        std::vector<char> read_back(LARGE_SIZE);

        // Fill with pattern
        for (size_t i = 0; i < LARGE_SIZE; ++i) {
            original[i] = static_cast<char>(i % 256);
        }

        REQUIRE(memory->ReadMemory(reinterpret_cast<uintptr_t>(original.data()), read_back.data(), LARGE_SIZE));
        REQUIRE(original == read_back);
    }

    SECTION("Boundary conditions") {
        char single_byte = 0xAB;
        char read_byte = 0;

        REQUIRE(memory->ReadMemory(reinterpret_cast<uintptr_t>(&single_byte), &read_byte, 1));
        REQUIRE(read_byte == single_byte);
    }
}