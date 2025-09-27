#include "AppContext.hpp"
#include "FontManager.hpp"
#include "SettingsPanel.hpp"
#include "WindowRegistry.hpp"

#include <SDL3/SDL.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>

#include <filesystem>

static void SDLCALL sdl_log_bridge(void* userdata, int category, SDL_LogPriority priority, const char* message)
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

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::filesystem::create_directories("logs");
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;
    plog::init(plog::info, "logs/run.log");
    if (auto logger = plog::get())
        logger->addAppender(&console_appender);

    AppContext app;
    if (!app.initialize())
        return 1;

    SDL_SetLogOutputFunction(sdl_log_bridge, nullptr);
    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    ImGuiIO& io = app.imguiIO();
    FontManager font_manager(io);
    WindowRegistry registry(font_manager, io);
    registry.createDialogWindow();

    SettingsPanel settings_panel(registry, font_manager, io);

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (app.processEvent(event))
            {
                running = false;
                break;
            }
        }

        if (!running)
            break;

        app.beginFrame();

        for (auto& window : registry.windows())
        {
            if (window)
                window->render(io);
        }

        settings_panel.render();

        app.endFrame();
        SDL_Delay(16);
    }

    return 0;
}
