#include "AppContext.hpp"
#include "utils/ErrorReporter.hpp"
#include "utils/NativeMessageBox.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <plog/Log.h>
#include <cmath>
#include <algorithm>
#include "ui/Localization.hpp"
#include "UITheme.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

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

    if (!initializeSDL()) return false;
    if (!createWindow()) return false;
    if (!createRenderer()) return false;
    if (!initializeImGui()) return false;

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

void AppContext::updateRendererScale()
{
    if (!window_ || !renderer_)
        return;

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
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

void AppContext::triggerVignette(float x, float y)
{
    vignette_active_ = true;
    vignette_time_ = 0.0f;
    vignette_center_x_ = x;
    vignette_center_y_ = y;
}

void AppContext::updateVignette(float delta_time)
{
    if (!vignette_active_)
        return;

    vignette_time_ += delta_time;
    if (vignette_time_ >= vignette_duration_)
    {
        vignette_active_ = false;
        vignette_time_ = 0.0f;
    }
}

void AppContext::renderVignette()
{
    if (!vignette_active_)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    
    const float half_duration = vignette_duration_ * 0.5f;
    const float max_alpha = 0.5f;
    
    float alpha = 0.0f;
    if (vignette_time_ < half_duration)
    {
        float normalized = vignette_time_ / half_duration;
        float eased = normalized * normalized * (3.0f - 2.0f * normalized);
        alpha = eased * max_alpha;
    }
    else
    {
        float normalized = (vignette_time_ - half_duration) / half_duration;
        float eased = normalized * normalized * (3.0f - 2.0f * normalized);
        alpha = (1.0f - eased) * max_alpha;
    }

    const float border_width = io.DisplaySize.x * 0.10f;
    const float border_height = io.DisplaySize.y * 0.10f;
    
    ImU32 transparent = IM_COL32(0, 0, 0, 0);
    ImU32 mask = IM_COL32(255, 222, 33, static_cast<int>(alpha * 255.0f));
    
    draw_list->AddRectFilledMultiColor(
        ImVec2(0, 0),
        ImVec2(io.DisplaySize.x, border_height),
        mask, mask, transparent, transparent
    );
    
    draw_list->AddRectFilledMultiColor(
        ImVec2(0, io.DisplaySize.y - border_height),
        ImVec2(io.DisplaySize.x, io.DisplaySize.y),
        transparent, transparent, mask, mask
    );
    
    draw_list->AddRectFilledMultiColor(
        ImVec2(0, 0),
        ImVec2(border_width, io.DisplaySize.y),
        mask, transparent, transparent, mask
    );
    
    draw_list->AddRectFilledMultiColor(
        ImVec2(io.DisplaySize.x - border_width, 0),
        ImVec2(io.DisplaySize.x, io.DisplaySize.y),
        transparent, mask, mask, transparent
    );
}

void AppContext::setWindowBorderless(bool borderless)
{
    if (!window_) return;
    SDL_SetWindowBordered(window_, !borderless);
}

void AppContext::setWindowAlwaysOnTop(bool topmost)
{
    if (!window_) return;
    SDL_SetWindowAlwaysOnTop(window_, topmost);
#ifdef _WIN32
    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
    void* hwnd_ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd_ptr)
    {
        HWND hwnd = reinterpret_cast<HWND>(hwnd_ptr);
        SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

void AppContext::maximizeWindow()
{
    if (!window_) return;
    SDL_MaximizeWindow(window_);
}

void AppContext::restoreWindow()
{
    if (!window_) return;
    SDL_RestoreWindow(window_);
}

void AppContext::setWindowSize(int w, int h)
{
    if (!window_) return;
    SDL_SetWindowSize(window_, w, h);
}

void AppContext::setWindowPosition(int x, int y)
{
    if (!window_) return;
    SDL_SetWindowPosition(window_, x, y);
}

void AppContext::getWindowPosition(int& x, int& y)
{
    if (!window_) { x = y = 0; return; }
    SDL_GetWindowPosition(window_, &x, &y);
}

void AppContext::getWindowSize(int& w, int& h)
{
    if (!window_) { w = h = 0; return; }
    SDL_GetWindowSize(window_, &w, &h);
}

#ifdef _WIN32
static HWND get_hwnd_from_sdl(SDL_Window* w)
{
    if (!w) return nullptr;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    void* hwnd_ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return reinterpret_cast<HWND>(hwnd_ptr);
}

#endif

bool AppContext::initializeSDL()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        reportInitError("SDL", "app.init.graphics_failed", std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }
    return true;
}

bool AppContext::createWindow()
{
    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow("DQX Utility", 1024, 800, window_flags);
    if (!window_)
    {
        reportInitError("Window", "app.init.window_failed", std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        shutdown();
        return false;
    }
    return true;
}

bool AppContext::createRenderer()
{
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
    {
        reportInitError("Renderer", "app.init.renderer_failed", std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        shutdown();
        return false;
    }
    
    if (!SDL_SetRenderVSync(renderer_, 1))
    {
        PLOG_WARNING << "Failed to enable VSync: " << SDL_GetError() << " (will continue without VSync)";
    }
    
    updateRendererScale();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    return true;
}

bool AppContext::initializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    ImGui::StyleColorsDark();
    UITheme::applyDockingTheme();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_))
    {
        reportInitError("ImGui SDL3 Backend", "app.init.ui_backend_failed", "ImGui_ImplSDL3_InitForSDLRenderer returned false");
        shutdown();
        return false;
    }
    
    if (!ImGui_ImplSDLRenderer3_Init(renderer_))
    {
        reportInitError("ImGui Renderer Backend", "app.init.ui_renderer_failed", "ImGui_ImplSDLRenderer3_Init returned false");
        shutdown();
        return false;
    }
    
    return true;
}

void AppContext::reportInitError(const char* phase, const std::string& i18n_key, const std::string& details)
{
    PLOG_FATAL << phase << " initialization failed: " << details;
    utils::ErrorReporter::ReportFatal(utils::ErrorCategory::Initialization, i18n::get(i18n_key.c_str()), details);
    utils::NativeMessageBox::ShowFatalError(i18n::get((i18n_key + "_long").c_str()), details);
}
