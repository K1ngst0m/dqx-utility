#include "WineDetector.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <plog/Log.h>

std::optional<WineEnvironment> WineDetector::detectWineEnvironment(const std::string& processName)
{
    if (processName.empty())
        return std::nullopt;
    
    // Step 1: Find single process by name
    auto pid = findSingleProcessByName(processName);
    if (!pid.has_value())
        return std::nullopt;
    
    PLOG_INFO << "Found single process " << processName << " with PID " << pid.value();
    
    // Step 2: Read process environment
    auto env = readProcessEnvironment(pid.value());
    if (env.empty())
    {
        PLOG_WARNING << "Failed to read environment for PID " << pid.value();
        return std::nullopt;
    }
    
    // Step 3: Extract Wine information
    auto wine_env = extractWineInfo(env);
    if (wine_env.wine_prefix.empty())
    {
        PLOG_WARNING << "No Wine environment detected for process " << processName;
        return std::nullopt;
    }
    
    // Step 4: Validate Wine prefix
    wine_env.validated = validateWinePrefix(wine_env.wine_prefix);
    
    PLOG_INFO << "Wine detection for " << processName << ": prefix=" << wine_env.wine_prefix 
              << ", binary=" << wine_env.wine_binary << ", method=" << wine_env.detection_method
              << ", validated=" << wine_env.validated;
    
    return wine_env;
}

std::optional<int> WineDetector::findSingleProcessByName(const std::string& processName)
{
    std::vector<int> matching_pids;
    
    std::filesystem::path proc_dir("/proc");
    if (!std::filesystem::exists(proc_dir))
        return std::nullopt;
    
    for (const auto& entry : std::filesystem::directory_iterator(proc_dir))
    {
        if (!entry.is_directory())
            continue;
        
        std::string dirname = entry.path().filename().string();
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(), ::isdigit))
            continue;
        
        int pid = std::stoi(dirname);
        
        // Check process command line
        std::filesystem::path cmdline_path = entry.path() / "cmdline";
        std::ifstream cmdline_file(cmdline_path);
        if (!cmdline_file.is_open())
            continue;
        
        std::string cmdline;
        std::getline(cmdline_file, cmdline, '\0');  // cmdline is null-separated
        
        if (cmdline.find(processName) != std::string::npos)
        {
            matching_pids.push_back(pid);
        }
    }
    
    if (matching_pids.empty())
    {
        PLOG_WARNING << "No processes found matching " << processName;
        return std::nullopt;
    }
    
    if (matching_pids.size() > 1)
    {
        PLOG_ERROR << "Multiple processes found matching " << processName << " (count: " 
                   << matching_pids.size() << "). Wine detection requires single instance.";
        return std::nullopt;
    }
    
    return matching_pids[0];
}

std::unordered_map<std::string, std::string> WineDetector::readProcessEnvironment(int pid)
{
    std::unordered_map<std::string, std::string> env;
    
    std::filesystem::path environ_path = std::filesystem::path("/proc") / std::to_string(pid) / "environ";
    std::ifstream environ_file(environ_path, std::ios::binary);
    
    if (!environ_file.is_open())
        return env;
    
    std::string buffer((std::istreambuf_iterator<char>(environ_file)), std::istreambuf_iterator<char>());
    
    // Split by null bytes
    size_t start = 0;
    for (size_t i = 0; i <= buffer.length(); ++i)
    {
        if (i == buffer.length() || buffer[i] == '\0')
        {
            if (i > start)
            {
                std::string entry = buffer.substr(start, i - start);
                size_t eq_pos = entry.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = entry.substr(0, eq_pos);
                    std::string value = entry.substr(eq_pos + 1);
                    env[key] = value;
                }
            }
            start = i + 1;
        }
    }
    
    return env;
}

WineEnvironment WineDetector::extractWineInfo(const std::unordered_map<std::string, std::string>& env)
{
    WineEnvironment wine_env;
    
    // Check for WINEPREFIX first (most reliable)
    auto it = env.find("WINEPREFIX");
    if (it != env.end() && !it->second.empty())
    {
        wine_env.wine_prefix = it->second;
        wine_env.detection_method = "WINEPREFIX";
    }
    
    // Check for WINELOADER (Wine binary)
    it = env.find("WINELOADER");
    if (it != env.end() && !it->second.empty())
    {
        wine_env.wine_binary = it->second;
        if (wine_env.detection_method.empty())
            wine_env.detection_method = "WINELOADER";
    }
    
    // Check for Proton environment variables
    it = env.find("STEAM_COMPAT_DATA_PATH");
    if (it != env.end() && !it->second.empty() && wine_env.wine_prefix.empty())
    {
        // For Proton, prefix is typically STEAM_COMPAT_DATA_PATH/pfx/
        wine_env.wine_prefix = it->second + "/pfx/";
        wine_env.detection_method = "PROTON_STEAM_COMPAT";
    }
    
    // Check WINEDLLPATH for Wine installation path
    it = env.find("WINEDLLPATH");
    if (it != env.end() && !it->second.empty() && wine_env.wine_binary.empty())
    {
        // Extract Wine binary path from WINEDLLPATH
        std::string dll_path = it->second;
        size_t colon_pos = dll_path.find(':');
        if (colon_pos != std::string::npos)
            dll_path = dll_path.substr(0, colon_pos);
        
        // Try to construct wine binary path
        if (dll_path.find("/lib/wine") != std::string::npos)
        {
            std::string bin_path = dll_path.substr(0, dll_path.find("/lib/wine")) + "/bin/wine";
            if (std::filesystem::exists(bin_path))
            {
                wine_env.wine_binary = bin_path;
                if (wine_env.detection_method.empty())
                    wine_env.detection_method = "WINEDLLPATH";
            }
        }
    }
    
    return wine_env;
}

bool WineDetector::validateWinePrefix(const std::string& prefix)
{
    if (prefix.empty())
        return false;
    
    std::filesystem::path prefix_path(prefix);
    if (!std::filesystem::exists(prefix_path) || !std::filesystem::is_directory(prefix_path))
        return false;
    
    // Check for Wine-specific files/directories
    std::vector<std::string> wine_markers = {
        "system.reg",
        "user.reg", 
        "drive_c",
        "dosdevices"
    };
    
    for (const std::string& marker : wine_markers)
    {
        std::filesystem::path marker_path = prefix_path / marker;
        if (std::filesystem::exists(marker_path))
            return true;
    }
    
    return false;
}