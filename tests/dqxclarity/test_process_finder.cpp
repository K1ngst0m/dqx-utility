#include <catch2/catch_test_macros.hpp>
#include <dqxclarity/process/ProcessFinder.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace dqxclarity;

TEST_CASE("ProcessFinder - GetCurrentProcessId", "[process]")
{
    SECTION("Returns valid process ID")
    {
        auto pid = ProcessFinder::GetCurrentProcessId();
        REQUIRE(pid > 0);
    }
    
    SECTION("Consistent across multiple calls")
    {
        auto pid1 = ProcessFinder::GetCurrentProcessId();
        auto pid2 = ProcessFinder::GetCurrentProcessId();
        REQUIRE(pid1 == pid2);
    }
}

TEST_CASE("ProcessFinder - IsProcessAlive", "[process]")
{
    SECTION("Current process is alive")
    {
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        REQUIRE(ProcessFinder::IsProcessAlive(current_pid));
    }
    
    SECTION("Invalid PID returns false")
    {
        // PID 0 is invalid on all platforms
        REQUIRE_FALSE(ProcessFinder::IsProcessAlive(0));
    }
    
    SECTION("Very high PID is unlikely to exist")
    {
        // Use a very high PID that's unlikely to exist
        pid_t unlikely_pid = 999999999;
        REQUIRE_FALSE(ProcessFinder::IsProcessAlive(unlikely_pid));
    }
}

TEST_CASE("ProcessFinder - GetRuntimeDirectory", "[process]")
{
    SECTION("Returns valid path")
    {
        auto runtime_dir = ProcessFinder::GetRuntimeDirectory();
        REQUIRE(!runtime_dir.empty());
    }
    
    SECTION("Path ends with .dqxu-runtime")
    {
        auto runtime_dir = ProcessFinder::GetRuntimeDirectory();
        REQUIRE(runtime_dir.filename() == ".dqxu-runtime");
    }
    
    SECTION("Directory is created if it doesn't exist")
    {
        auto runtime_dir = ProcessFinder::GetRuntimeDirectory();
        
        // Directory should exist after calling GetRuntimeDirectory
        REQUIRE(std::filesystem::exists(runtime_dir));
        REQUIRE(std::filesystem::is_directory(runtime_dir));
    }
    
    SECTION("Consistent across multiple calls")
    {
        auto dir1 = ProcessFinder::GetRuntimeDirectory();
        auto dir2 = ProcessFinder::GetRuntimeDirectory();
        REQUIRE(dir1 == dir2);
    }
    
    SECTION("Path is writable")
    {
        auto runtime_dir = ProcessFinder::GetRuntimeDirectory();
        auto test_file = runtime_dir / "test_write.tmp";
        
        // Try to create a file
        std::ofstream test(test_file);
        REQUIRE(test.is_open());
        test << "test";
        test.close();
        
        // Verify file exists
        REQUIRE(std::filesystem::exists(test_file));
        
        // Cleanup
        std::filesystem::remove(test_file);
    }
}

TEST_CASE("ProcessFinder - GetProcessInfo", "[process]")
{
    SECTION("Get current process info")
    {
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        
        REQUIRE(info.has_value());
        REQUIRE(info->pid == current_pid);
        REQUIRE(!info->exe_path.empty());
    }
    
    SECTION("Invalid PID returns nullopt")
    {
        auto info = ProcessFinder::GetProcessInfo(0);
        REQUIRE_FALSE(info.has_value());
    }
    
    SECTION("Process info contains executable path")
    {
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        
        REQUIRE(info.has_value());
        // Path should contain the test executable name
        bool has_test_name = (info->exe_path.find("test") != std::string::npos ||
                             info->exe_path.find("dqxu") != std::string::npos);
        REQUIRE(has_test_name);
    }
}

TEST_CASE("ProcessFinder - FindByName", "[process]")
{
    SECTION("Can find current process by name")
    {
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        auto current_info = ProcessFinder::GetProcessInfo(current_pid);
        REQUIRE(current_info.has_value());
        
        // Extract just the executable name without path
        std::filesystem::path exe_path(current_info->exe_path);
        std::string exe_name = exe_path.filename().string();
        
        auto pids = ProcessFinder::FindByName(exe_name);
        
        // Should find at least our own process
        REQUIRE(!pids.empty());
        REQUIRE(std::find(pids.begin(), pids.end(), current_pid) != pids.end());
    }
    
    SECTION("Non-existent process name returns empty")
    {
        auto pids = ProcessFinder::FindByName("ThisProcessNameShouldNeverExist12345");
        REQUIRE(pids.empty());
    }
    
    SECTION("Case-insensitive search works")
    {
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        auto current_info = ProcessFinder::GetProcessInfo(current_pid);
        REQUIRE(current_info.has_value());
        
        std::filesystem::path exe_path(current_info->exe_path);
        std::string exe_name = exe_path.filename().string();
        
        // Convert to uppercase
        std::string upper_name = exe_name;
        std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
        
        auto pids = ProcessFinder::FindByName(upper_name, false);
        REQUIRE(std::find(pids.begin(), pids.end(), current_pid) != pids.end());
    }
}

TEST_CASE("ProcessFinder - Integration tests", "[process][integration]")
{
    SECTION("Runtime directory and process info consistency")
    {
        auto runtime_dir = ProcessFinder::GetRuntimeDirectory();
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        
        REQUIRE(info.has_value());
        
        // Runtime dir should be sibling to executable
        std::filesystem::path exe_path(info->exe_path);
        std::filesystem::path expected_parent = exe_path.parent_path();
        
        REQUIRE(runtime_dir.parent_path() == expected_parent);
    }
    
    SECTION("Process lifecycle detection")
    {
        // Get current process
        auto current_pid = ProcessFinder::GetCurrentProcessId();
        
        // Should be alive
        REQUIRE(ProcessFinder::IsProcessAlive(current_pid));
        
        // Should have valid info
        auto info = ProcessFinder::GetProcessInfo(current_pid);
        REQUIRE(info.has_value());
        REQUIRE(info->pid == current_pid);
    }
}
