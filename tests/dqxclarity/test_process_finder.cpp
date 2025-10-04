#include <catch2/catch_test_macros.hpp>
#include "dqxclarity/process/ProcessFinder.hpp"
#include <unistd.h>

using namespace dqxclarity;

TEST_CASE("ProcessFinder - Get current process info", "[process][finder]") {
    pid_t current_pid = getpid();

    SECTION("GetProcessInfo for current process") {
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        REQUIRE(info.has_value());
        REQUIRE(info->pid == current_pid);
        REQUIRE(!info->name.empty());
        REQUIRE(!info->exe_path.empty());
    }

    SECTION("FindByName finds current process") {
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        REQUIRE(info.has_value());

        auto pids = ProcessFinder::FindByName(info->name, false);
        bool found = false;
        for (pid_t pid : pids) {
            if (pid == current_pid) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("ProcessFinder - Invalid process", "[process][finder]") {
    SECTION("GetProcessInfo for invalid PID") {
        auto info = ProcessFinder::GetProcessInfo(999999);
        REQUIRE_FALSE(info.has_value());
    }

    SECTION("FindByName for non-existent process") {
        auto pids = ProcessFinder::FindByName("this_process_definitely_does_not_exist_12345", false);
        REQUIRE(pids.empty());
    }
}

TEST_CASE("ProcessFinder - Case sensitivity", "[process][finder]") {
    pid_t current_pid = getpid();
    auto info = ProcessFinder::GetProcessInfo(current_pid);
    REQUIRE(info.has_value());

    std::string lowercase_name = info->name;
    std::transform(lowercase_name.begin(), lowercase_name.end(), lowercase_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    SECTION("Case-insensitive search") {
        auto pids_lower = ProcessFinder::FindByName(lowercase_name, false);
        REQUIRE(!pids_lower.empty());
    }

    SECTION("Case-sensitive search") {
        if (lowercase_name != info->name) {
            auto pids_sensitive = ProcessFinder::FindByName(lowercase_name, true);
            bool found_exact = false;
            for (pid_t pid : pids_sensitive) {
                auto proc_info = ProcessFinder::GetProcessInfo(pid);
                if (proc_info.has_value() && proc_info->name == lowercase_name) {
                    found_exact = true;
                    break;
                }
            }
            REQUIRE_FALSE(found_exact);
        }
    }
}

TEST_CASE("ProcessFinder - FindAll", "[process][finder]") {
    auto all_processes = ProcessFinder::FindAll();

    SECTION("Returns non-empty list") {
        REQUIRE(!all_processes.empty());
    }

    SECTION("Contains current process") {
        pid_t current_pid = getpid();
        bool found = false;
        for (const auto& proc : all_processes) {
            if (proc.pid == current_pid) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }

    SECTION("All entries have valid PIDs") {
        for (const auto& proc : all_processes) {
            REQUIRE(proc.pid > 0);
            REQUIRE(!proc.name.empty());
        }
    }
}

TEST_CASE("ProcessFinder - FindByExePath", "[process][finder]") {
    pid_t current_pid = getpid();
    auto info = ProcessFinder::GetProcessInfo(current_pid);
    REQUIRE(info.has_value());
    REQUIRE(!info->exe_path.empty());

    SECTION("Find by exact exe path") {
        auto pids = ProcessFinder::FindByExePath(info->exe_path);
        bool found = false;
        for (pid_t pid : pids) {
            if (pid == current_pid) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}
