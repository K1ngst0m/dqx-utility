#include "AppContext.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <plog/Log.h>
#include <cmath>

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

    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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

    updateRendererScale();

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

    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_RESIZED)
    {
        if (window_ && renderer_)
        {
            const SDL_WindowID wid = SDL_GetWindowID(window_);
            SDL_WindowEvent win_ev = event.window;
            if (win_ev.windowID == wid)
            {
                is_resizing_ = true;
                updateRendererScale();
            }
        }
    }

    if (event.type == SDL_EVENT_WINDOW_MOUSE_ENTER || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        if (window_)
        {
            const SDL_WindowID wid = SDL_GetWindowID(window_);
            SDL_WindowEvent win_ev = event.window;
            if (win_ev.windowID == wid)
                is_resizing_ = false;
        }
    }

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

void AppContext::updateRendererScale()
{
    if (!window_ || !renderer_)
        return;

    const Uint32 flags = SDL_GetWindowFlags(window_);
    if (flags & SDL_WINDOW_MINIMIZED)
        return;

    int w = 0, h = 0;
    int pw = 0, ph = 0;
    SDL_GetWindowSize(window_, &w, &h);
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);

    float sx = (w > 0) ? (float)pw / (float)w : 1.0f;
    float sy = (h > 0) ? (float)ph / (float)h : 1.0f;
    if (sx <= 0.0f || !std::isfinite(sx)) sx = 1.0f;
    if (sy <= 0.0f || !std::isfinite(sy)) sy = 1.0f;

    auto quantize = [](float v) {
        return std::round(v * 1000.0f) / 1000.0f;
    };
    sx = quantize(sx);
    sy = quantize(sy);

    float curx = 1.0f, cury = 1.0f;
    SDL_GetRenderScale(renderer_, &curx, &cury);
    float qcurx = quantize(curx);
    float qcury = quantize(cury);
    if (qcurx == sx && qcury == sy)
        return;

    if (SDL_SetRenderScale(renderer_, sx, sy) != 0)
    {
        if (is_resizing_)
        {
            PLOG_DEBUG << "SDL_SetRenderScale(" << sx << "," << sy << ") failed during resize: " << SDL_GetError();
        }
        else
        {
            PLOG_WARNING << "SDL_SetRenderScale(" << sx << "," << sy << ") failed: " << SDL_GetError()
                         << " w=" << w << " h=" << h << " pw=" << pw << " ph=" << ph
                         << " curx=" << curx << " cury=" << cury;
        }
    }
}
