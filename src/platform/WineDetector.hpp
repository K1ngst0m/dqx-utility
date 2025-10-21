#pragma once

#include <string>
#include <optional>
#include <unordered_map>

struct WineEnvironment
{
    std::string wine_binary;
    std::string wine_prefix;
    std::string detection_method;
    bool validated = false;
};

class WineDetector
{
public:
    // Detects Wine environment for a given process name
    // Returns empty optional if process not found or multiple instances found
    static std::optional<WineEnvironment> detectWineEnvironment(const std::string& processName);

private:
    // Find all PIDs matching process name, returns empty if not exactly one match
    static std::optional<int> findSingleProcessByName(const std::string& processName);

    // Read environment variables from /proc/{pid}/environ
    static std::unordered_map<std::string, std::string> readProcessEnvironment(int pid);

    // Extract Wine information from environment variables
    static WineEnvironment extractWineInfo(const std::unordered_map<std::string, std::string>& env);

    // Validate Wine prefix by checking for Wine-specific files/directories
    static bool validateWinePrefix(const std::string& prefix);
};