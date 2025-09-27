#include <SDL3/SDL.h>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
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
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::info, "logs/run.log");
    if (auto logger = plog::get()) logger->addAppender(&consoleAppender);
    SDL_SetLogOutputFunction(sdl_log_bridge, nullptr);

    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT;
    SDL_Window* window     = SDL_CreateWindow("DQX Utility", 800, 600, win_flags);
    if (!window)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer)
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    bool running = true;
    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
                running = false;
        }

        if (renderer)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            int render_width = 0;
            int render_height = 0;
            if (!SDL_GetRenderOutputSize(renderer, &render_width, &render_height))
            {
                SDL_GetWindowSize(window, &render_width, &render_height);
            }

            SDL_FRect rect;
            rect.w = 160.0f;
            rect.h = 120.0f;
            rect.x = (static_cast<float>(render_width) - rect.w) * 0.5f;
            rect.y = (static_cast<float>(render_height) - rect.h) * 0.5f;

            SDL_SetRenderDrawColor(renderer, 255, 128, 0, 255);
            SDL_RenderFillRect(renderer, &rect);

            SDL_RenderPresent(renderer);
        }

        SDL_Delay(16);
    }

    if (renderer)
        SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
