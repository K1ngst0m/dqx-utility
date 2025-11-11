#pragma once

#include <memory>
#include <SDL3/SDL.h>

class AppContext;
class FontManager;
class GlobalStateManager;
class WindowRegistry;
class ConfigManager;
class GlobalSettingsPanel;
class ErrorDialog;
class SingleInstanceGuard;
class QuestManager;
class MonsterManager;

namespace processing
{
class GlossaryManager;
}

namespace updater
{
class UpdaterService;
}

namespace ui
{
class UIEventHandler;
class MiniModeManager;
class AppModeManager;
} // namespace ui

class Application
{
public:
    Application(int argc, char** argv);
    ~Application();

    int run();
    void requestExit();

    WindowRegistry* registry() { return registry_.get(); }

private:
    bool initialize();
    bool initializeLogging();
    void initializeConsole();
    void setupSDLLogging();
    void setupManagers();
    void initializeConfig();

    void mainLoop();
    float calculateDeltaTime();
    void processEvents();
    void handleModeChanges();

    void renderFrame(float deltaTime);
    void setupMiniModeDockspace();
    void renderWindows();
    void handleUIRequests();

    void handleQuitRequests();
    void cleanup();

    bool checkSingleInstance();
    void parseCommandLineArgs();

    std::unique_ptr<AppContext> context_;
    std::unique_ptr<FontManager> font_manager_;
    std::unique_ptr<GlobalStateManager> global_state_;
    std::unique_ptr<WindowRegistry> registry_;
    std::unique_ptr<ConfigManager> config_;
    std::unique_ptr<GlobalSettingsPanel> settings_panel_;
    std::unique_ptr<ErrorDialog> error_dialog_;
    std::unique_ptr<SingleInstanceGuard> instance_guard_;

    std::unique_ptr<ui::UIEventHandler> event_handler_;
    std::unique_ptr<ui::MiniModeManager> mini_manager_;
    std::unique_ptr<ui::AppModeManager> mode_manager_;
    std::unique_ptr<updater::UpdaterService> updater_service_;
    std::unique_ptr<QuestManager> quest_manager_;
    std::unique_ptr<MonsterManager> monster_manager_;
    std::unique_ptr<processing::GlossaryManager> glossary_manager_;

    bool show_settings_ = false;
    bool quit_requested_ = false;
    bool running_ = true;
    Uint64 last_time_ = 0;
    bool last_window_topmost_ = false;

    // ImGui metrics window
    bool show_imgui_metrics_ = false;

    [[maybe_unused]] int argc_ = 0;
    [[maybe_unused]] char** argv_ = nullptr;
};

