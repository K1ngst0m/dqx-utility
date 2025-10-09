#pragma once

#include <SDL3/SDL.h>
#include <plog/Log.h>

namespace platform {

/// Platform-specific initialization and utilities
class PlatformSetup
{
public:
    /// Initialize platform-specific console settings (UTF-8 on Windows)
    static void InitializeConsole();
    
    /// Bridge SDL log messages to plog
    static void SDLCALL SDLLogBridge(void* userdata, int category, SDL_LogPriority priority, const char* message);
    
    /// Setup SDL logging to use plog
    static void SetupSDLLogging();
};

} // namespace platform
