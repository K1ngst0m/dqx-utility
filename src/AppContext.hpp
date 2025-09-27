#pragma once

#include <SDL3/SDL.h>

struct ImGuiIO;

// AppContext owns SDL and ImGui initialization and provides per-frame helpers.
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

private:
    SDL_Window* window_        = nullptr;
    SDL_Renderer* renderer_    = nullptr;
    bool initialized_          = false;
};
