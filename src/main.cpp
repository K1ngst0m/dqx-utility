// Application main entry point
// Handles initialization, main loop, and app mode management (Normal, Borderless, Mini)

#include "AppContext.hpp"
#include "FontManager.hpp"
#include "GlobalSettingsPanel.hpp"
#include "WindowRegistry.hpp"
#include "dialog/DialogWindow.hpp"
#include "config/ConfigManager.hpp"
#include "utils/ErrorReporter.hpp"
#include "utils/CrashHandler.hpp"
#include "ErrorDialog.hpp"
#include "ui/Localization.hpp"
#include "ui/DockState.hpp"
#include "ui/UIEventHandler.hpp"
#include "ui/MiniModeManager.hpp"
#include "services/DQXClarityService.hpp"
#include "platform/PlatformSetup.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <toml++/toml.h>

// ============================================================================
// App mode management
// ============================================================================

static void apply_app_mode_window_settings(AppContext& app, ConfigManager::AppMode mode)
{
    switch (mode)
    {
    case ConfigManager::AppMode::Mini:
        app.setWindowBorderless(true);
        app.restoreWindow();
        app.setWindowSize(600, 800);
        break;
        
    case ConfigManager::AppMode::Borderless:
        app.setWindowBorderless(true);
        app.maximizeWindow();
        break;
        
    case ConfigManager::AppMode::Normal:
        app.setWindowBorderless(false);
        app.restoreWindow();
        app.setWindowSize(1024, 800);
        break;
    }
}

static void handle_app_mode_change(AppContext& app, WindowRegistry& registry, 
                                   ConfigManager::AppMode old_mode, ConfigManager::AppMode new_mode)
{
    apply_app_mode_window_settings(app, new_mode);
    
    // When leaving Mini mode, restore dialog positions
    if (old_mode == ConfigManager::AppMode::Mini && new_mode != ConfigManager::AppMode::Mini)
    {
        ui::MiniModeManager mini_manager(app, registry);
        mini_manager.RestoreDialogsFromMiniMode();
    }
    
    DockState::RequestReDock();
}


// ============================================================================
// Application initialization
// ============================================================================

static bool initialize_logging(int argc, char** argv)
{
    std::filesystem::create_directories("logs");
    
    // Check config file for append_logs setting
    bool append_logs = true;
    try {
        std::ifstream ifs("config.toml");
        if (ifs) {
            toml::table cfg = toml::parse(ifs);
            if (auto* g = cfg["global"].as_table()) {
                if (auto v = (*g)["append_logs"].value<bool>()) {
                    append_logs = *v;
                }
            }
        }
    } catch (...) {
        // Ignore parse errors during early init
    }
    
    // Check command-line override
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--append-logs") == 0) {
            append_logs = true;
        }
    }
    
    // Initialize plog with file and console output
    static plog::RollingFileAppender<plog::TxtFormatter> file_appender("logs/run.log", 1024 * 1024 * 10, 3);
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;
    plog::init(plog::info, &file_appender);
    if (auto logger = plog::get())
        logger->addAppender(&console_appender);
    
    return true;
}

// ============================================================================
// Main entry point
// ============================================================================

int main(int argc, char** argv)
{
    utils::CrashHandler::Initialize();
    initialize_logging(argc, argv);
    
    platform::PlatformSetup::InitializeConsole();

    AppContext app;
    if (!app.initialize())
        return 1;

    platform::PlatformSetup::SetupSDLLogging();
    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");

    ImGuiIO& io = app.imguiIO();
    FontManager font_manager(io);
    WindowRegistry registry(font_manager, io);

    // Initialize config manager
    static ConfigManager cfg_mgr;
    ConfigManager_Set(&cfg_mgr);
    cfg_mgr.setRegistry(&registry);
    cfg_mgr.loadAtStartup();
    
    // Initialize localization
    i18n::init(cfg_mgr.getUILanguageCode());
    
    // Create initial dialog if none exist
    if (registry.windowsByType(UIWindowType::Dialog).empty())
        registry.createDialogWindow();
    
    // UI components
    GlobalSettingsPanel settings_panel(registry);
    ErrorDialog error_dialog;
    
    // UI utility managers
    ui::UIEventHandler event_handler(app, registry);
    ui::MiniModeManager mini_manager(app, registry);
    
    // Application state
    bool show_manager = true;
    bool quit_requested = false;
    bool running = true;
    Uint64 last_time = SDL_GetTicks();
    
    // Start in Normal mode for consistent layout
    cfg_mgr.setAppMode(ConfigManager::AppMode::Normal);
    apply_app_mode_window_settings(app, ConfigManager::AppMode::Normal);
    ConfigManager::AppMode last_app_mode = ConfigManager::AppMode::Normal;

    // ========================================================================
    // Main loop
    // ========================================================================
    while (running)
    {
        // Frame timing
        Uint64 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        last_time = current_time;
        
        // Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (app.processEvent(event))
                quit_requested = true;
        }
        
        if (!running)
            break;
        
        app.updateVignette(delta_time);
        
        // Handle app mode changes
        ConfigManager::AppMode current_mode = cfg_mgr.getAppMode();
        if (current_mode != last_app_mode)
        {
            handle_app_mode_change(app, registry, last_app_mode, current_mode);
            last_app_mode = current_mode;
        }
        
        // Begin ImGui frame
        app.beginFrame();
        
        // Setup dockspace for Mini mode
        ImGuiID dockspace_id = 0;
        if (current_mode == ConfigManager::AppMode::Mini)
            dockspace_id = mini_manager.SetupDockspace();
        DockState::SetDockspace(dockspace_id);
        
        // Render all windows
        for (auto& window : registry.windows())
        {
            if (window)
                window->render(io);
        }
        
        registry.processRemovals();
        
        // Mini mode: Alt+Drag to move window
        if (current_mode == ConfigManager::AppMode::Mini)
            mini_manager.HandleAltDrag();

        // Handle transparent area clicks and context menu
        event_handler.HandleTransparentAreaClick(io);
        event_handler.RenderGlobalContextMenu(io, show_manager, quit_requested);
        
        // Handle UI requests from dialog context menus
        if (cfg_mgr.isGlobalSettingsRequested())
        {
            show_manager = true;
            cfg_mgr.consumeGlobalSettingsRequest();
        }
        if (cfg_mgr.isQuitRequested())
        {
            quit_requested = true;
            cfg_mgr.consumeQuitRequest();
        }
        
        // Render settings panel
        if (show_manager)
            settings_panel.render(show_manager);
        
        // Handle errors
        if (utils::ErrorReporter::HasPendingErrors())
        {
            auto errors = utils::ErrorReporter::GetPendingErrors();
            error_dialog.Show(errors);
        }
        
        if (error_dialog.Render())
            quit_requested = true;
        
        // Handle quit
        if (quit_requested)
        {
            if (auto* dqxc = DQXClarityService_Get())
                dqxc->stop();
            cfg_mgr.saveAll();
            running = false;
        }
        
        // Finalize frame
        DockState::ConsumeReDock();
        app.renderVignette();
        app.endFrame();
        SDL_Delay(16);
    }
    
    // Cleanup
    if (auto* cm = ConfigManager_Get())
        cm->saveAll();
    
    return 0;
}
