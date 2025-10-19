#pragma once

#include <memory>
#include <SDL3/SDL.h>

class AppContext;
class FontManager;
class WindowRegistry;
class ConfigManager;
class GlobalSettingsPanel;
class ErrorDialog;
class SingleInstanceGuard;

namespace ui {
class UIEventHandler;
class MiniModeManager;
class AppModeManager;
}

class Application
{
public:
    Application(int argc, char** argv);
    ~Application();

    int run();

private:
    bool initialize();
    void setupManagers();
    void initializeConfig();
    void mainLoop();
    void handleModeChanges();
    void renderFrame(float deltaTime);
    void handleQuitRequests();
    void cleanup();

    bool initializeLogging();
    void initializeConsole();
    void setupSDLLogging();

    std::unique_ptr<AppContext> context_;
    std::unique_ptr<FontManager> font_manager_;
    std::unique_ptr<WindowRegistry> registry_;
    std::unique_ptr<ConfigManager> config_;
    std::unique_ptr<GlobalSettingsPanel> settings_panel_;
    std::unique_ptr<ErrorDialog> error_dialog_;
    std::unique_ptr<SingleInstanceGuard> instance_guard_;
    
    std::unique_ptr<ui::UIEventHandler> event_handler_;
    std::unique_ptr<ui::MiniModeManager> mini_manager_;
    std::unique_ptr<ui::AppModeManager> mode_manager_;
    
    bool show_settings_ = false;
    bool quit_requested_ = false;
    bool running_ = true;
    Uint64 last_time_ = 0;
    bool last_window_topmost_ = false;

    int argc_ = 0;
    char** argv_ = nullptr;
    bool force_verbose_pipeline_ = false;
};
