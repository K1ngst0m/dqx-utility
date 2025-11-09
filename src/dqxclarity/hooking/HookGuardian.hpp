#pragma once

#include <chrono>
#include <string>

namespace dqxclarity
{
namespace persistence
{

class HookGuardian
{
public:
    // Main guardian loop, called from separate process context
    static int RunGuardianLoop();
    
    // Start guardian process from main application
    static bool StartGuardian();
    
    // Update heartbeat from main application (call periodically)
    static void UpdateHeartbeat();
    
    // Signal guardian to exit gracefully
    static void SignalShutdown();
    
private:
    // Check if DQXGame.exe is running
    static bool IsDQXGameRunning();
    
    // Check if client application process is alive
    static bool IsMainProcessAlive();
    
    // Get heartbeat file path
    static std::string GetHeartbeatPath();
    
    // Get shutdown signal file path
    static std::string GetShutdownSignalPath();
    
    // Constants
    static constexpr auto kHeartbeatTimeout = std::chrono::seconds(5);
    static constexpr auto kCheckInterval = std::chrono::milliseconds(1000);
    static constexpr auto kMainProcessCheckInterval = std::chrono::milliseconds(500);
};

} // namespace persistence
} // namespace dqxclarity
