#pragma once

#include <SDL3/SDL.h>

struct ImGuiIO;

class AppContext
{
public:
    AppContext();
    ~AppContext();

    bool initialize();
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame();
    void endFrame();

    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    ImGuiIO& imguiIO();

    void triggerVignette(float x, float y);
    void updateVignette(float delta_time);
    void renderVignette();

private:
    void updateRendererScale();

    SDL_Window* window_        = nullptr;
    SDL_Renderer* renderer_    = nullptr;
    bool initialized_          = false;
    bool is_resizing_          = false;

    bool vignette_active_      = false;
    float vignette_time_       = 0.0f;
    float vignette_center_x_   = 0.0f;
    float vignette_center_y_   = 0.0f;
    static constexpr float vignette_duration_ = 1.0f;
};
