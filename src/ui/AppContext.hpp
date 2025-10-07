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

    // Toggle OS-level window decorations (title bar, borders)
    void setWindowBorderless(bool borderless);
    void maximizeWindow();
    void restoreWindow();
    void setWindowSize(int w, int h);

    // Move window on screen
    void setWindowPosition(int x, int y);
    void getWindowPosition(int& x, int& y);

    // Enable/disable OS-native drag/resize hit-testing for borderless windows
    void enableHitTest(bool enable, int drag_height_px = 28, int border_px = 8);
    void setHitTestDragHeight(int drag_height_px);
    void setHitTestBorder(int border_px);
    void setHitTestSideMargin(int side_margin_px);

    // Start native OS drag/resize (Windows only implementations)
    void beginNativeMove();
    void beginNativeResizeLeft();
    void beginNativeResizeRight();
    void beginNativeResizeTop();
    void beginNativeResizeBottom();
    void beginNativeResizeTopLeft();
    void beginNativeResizeTopRight();
    void beginNativeResizeBottomLeft();
    void beginNativeResizeBottomRight();

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

    // Hit-test configuration for borderless dragging/resizing
    bool hit_test_enabled_ = false;
    int hit_drag_height_px_ = 28;
    int hit_border_px_ = 8;
    int hit_drag_side_px_ = 64; // Only allow dragging near left/right edges of the top band to avoid tab clicks

    static SDL_HitTestResult SDLCALL HitTestCallback(SDL_Window* win, const SDL_Point* area, void* data);

#ifdef _WIN32
    void nativeNCClick(int ht_code);
#endif
};
