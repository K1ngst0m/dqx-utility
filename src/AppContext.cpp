#include "AppContext.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <plog/Log.h>

// Constructs an empty context waiting for initialization.
AppContext::AppContext() = default;

// Ensures SDL and ImGui state is cleaned up.
AppContext::~AppContext()
{
    shutdown();
}

// Bootstraps SDL window/renderer and ImGui backends.
bool AppContext::initialize()
{
    if (initialized_)
        return true;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        PLOG_FATAL << "SDL_Init failed: " << SDL_GetError();
        return false;
    }

    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT;
    window_ = SDL_CreateWindow("DQX Utility", 800, 600, window_flags);
    if (!window_)
    {
        PLOG_FATAL << "SDL_CreateWindow failed: " << SDL_GetError();
        shutdown();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
    {
        PLOG_FATAL << "SDL_CreateRenderer failed: " << SDL_GetError();
        shutdown();
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_))
    {
        PLOG_FATAL << "ImGui_ImplSDL3_InitForSDLRenderer failed";
        shutdown();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer_))
    {
        PLOG_FATAL << "ImGui_ImplSDLRenderer3_Init failed";
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

// Tears down ImGui, renderer, window, and SDL state.
void AppContext::shutdown()
{
    if (!initialized_)
        return;

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (renderer_)
    {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
    initialized_ = false;
}

// Forwards events to ImGui and reports platform quit requests.
bool AppContext::processEvent(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
    return event.type == SDL_EVENT_QUIT;
}

// Prepares a new ImGui frame.
void AppContext::beginFrame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

// Finalizes the ImGui frame and presents the renderer.
void AppContext::endFrame()
{
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

// Grants mutable access to ImGui IO state.
ImGuiIO& AppContext::imguiIO()
{
    return ImGui::GetIO();
}
