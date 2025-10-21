#pragma once

#include <SDL3/SDL.h>
#include <string>

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

    void triggerVignette(float x, float y);
    void updateVignette(float delta_time);
    void renderVignette();

    // Toggle OS-level window decorations (title bar, borders)
    void setWindowBorderless(bool borderless);
    void setWindowAlwaysOnTop(bool topmost);
    void maximizeWindow();
    void restoreWindow();
    void setWindowSize(int w, int h);

private:
    void updateRendererScale();

    // Initialization phase helpers
    bool initializeSDL();
    bool createWindow();
    bool createRenderer();
    bool initializeImGui();
    void reportInitError(const char* phase, const std::string& i18n_key, const std::string& details);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool initialized_ = false;
    bool is_resizing_ = false;

    bool vignette_active_ = false;
    float vignette_time_ = 0.0f;
    float vignette_center_x_ = 0.0f;
    float vignette_center_y_ = 0.0f;
    static constexpr float vignette_duration_ = 1.0f;
};
