#include "PlatformSetup.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <clocale>
#endif

namespace platform {

void PlatformSetup::InitializeConsole()
{
#ifdef _WIN32
    // Set Windows console to UTF-8 so Japanese text displays correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
        }
    }
    
    std::setlocale(LC_ALL, ".UTF-8");
#endif
}

void SDLCALL PlatformSetup::SDLLogBridge(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE: PLOG_VERBOSE << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_DEBUG:   PLOG_DEBUG   << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_INFO:    PLOG_INFO    << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_WARN:    PLOG_WARNING << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_ERROR:   PLOG_ERROR   << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_CRITICAL:PLOG_FATAL   << "[SDL:" << category << "] " << message; break;
    default:                       PLOG_INFO    << "[SDL:" << category << "] " << message; break;
    }
}

void PlatformSetup::SetupSDLLogging()
{
    SDL_SetLogOutputFunction(SDLLogBridge, nullptr);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
}

} // namespace platform
