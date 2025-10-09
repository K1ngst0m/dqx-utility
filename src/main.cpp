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
#include "services/DQXClarityService.hpp"

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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <clocale>

// ============================================================================
// Windows-specific helpers
// ============================================================================

static void SetupUtf8Console()
{
    // Set Windows console to UTF-8 so Japanese text displays correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
        }
    }
    
    std::setlocale(LC_ALL, ".UTF-8");
}
#endif

// ============================================================================
// Logging and event handling
// ============================================================================

static void SDLCALL sdl_log_bridge(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    switch (priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE: PLOG_VERBOSE << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_DEBUG:   PLOG_DEBUG   << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_INFO:    PLOG_INFO    << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_WARN:    PLOG_WARNING << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_ERROR:   PLOG_ERROR   << "[SDL:" << category << "] " << message; break;
    case SDL_LOG_PRIORITY_CRITICAL:PLOG_FATAL   << "[SDL:" << category << "] " << message; break;
    default:                       PLOG_INFO    << "[SDL:" << category << "] " << message; break;
    }
}

// ============================================================================
// UI helper functions
// ============================================================================

static bool is_mouse_outside_dialogs(ImGuiIO& io, WindowRegistry& registry)
{
    if (!ImGui::IsMousePosValid(&io.MousePos))
        return false;

    auto dialogs = registry.windowsByType(UIWindowType::Dialog);
    for (auto* window : dialogs)
    {
        auto* dialog = dynamic_cast<DialogWindow*>(window);
        if (dialog)
        {
            const auto& state = dialog->state();
            bool within_dialog = ImGui::IsMouseHoveringRect(
                state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y),
                false);
            if (within_dialog)
                return false;
        }
    }
    return true;
}

static void handle_transparent_area_click(ImGuiIO& io, WindowRegistry& registry, AppContext& app)
{
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;
    
    if (io.WantCaptureMouse)
        return;
    
    if (!is_mouse_outside_dialogs(io, registry))
        return;
    
    app.triggerVignette(io.MousePos.x, io.MousePos.y);
}

static void render_global_context_menu(ImGuiIO& io, WindowRegistry& registry, bool& show_manager, bool& quit_requested)
{
    if (is_mouse_outside_dialogs(io, registry) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("GlobalContextMenu");
    }

    if (ImGui::BeginPopup("GlobalContextMenu"))
    {
        if (ImGui::MenuItem(i18n::get("menu.global_settings")))
            show_manager = true;

        if (ImGui::BeginMenu(i18n::get("menu.app_mode")))
        {
            if (auto* cm = ConfigManager_Get())
            {
                auto mode = cm->getAppMode();
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.normal"), nullptr, mode == ConfigManager::AppMode::Normal))
                    cm->setAppMode(ConfigManager::AppMode::Normal);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.borderless"), nullptr, mode == ConfigManager::AppMode::Borderless))
                    cm->setAppMode(ConfigManager::AppMode::Borderless);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.mini"), nullptr, mode == ConfigManager::AppMode::Mini))
                    cm->setAppMode(ConfigManager::AppMode::Mini);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(i18n::get("menu.quit")))
            quit_requested = true;
        
        ImGui::EndPopup();
    }
}

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

static void restore_dialogs_from_mini_mode(WindowRegistry& registry)
{
    float y_offset = 0.0f;
    for (auto& window : registry.windows())
    {
        if (!window || window->type() != UIWindowType::Dialog)
            continue;
            
        if (auto* dw = dynamic_cast<DialogWindow*>(window.get()))
        {
            auto& ui = dw->state().ui_state();
            ui.width = 800.0f;
            ui.height = 600.0f;
            ui.window_pos = ImVec2(0.0f, y_offset);
            ui.pending_resize = true;
            ui.pending_reposition = true;
            y_offset += 40.0f;
        }
    }
}

static void handle_app_mode_change(AppContext& app, WindowRegistry& registry, 
                                   ConfigManager::AppMode old_mode, ConfigManager::AppMode new_mode)
{
    apply_app_mode_window_settings(app, new_mode);
    
    // When leaving Mini mode, restore dialog positions
    if (old_mode == ConfigManager::AppMode::Mini && new_mode != ConfigManager::AppMode::Mini)
    {
        restore_dialogs_from_mini_mode(registry);
    }
    
    DockState::RequestReDock();
}

// ============================================================================
// Mini mode: Alt+Drag to move window
// ============================================================================

static void handle_mini_mode_alt_drag(AppContext& app)
{
    static bool drag_triggered = false;
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Only enable drag when Alt key is held
    if (io.KeyAlt)
    {
        // Show hand cursor when Alt is held
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        
        // Trigger native OS window drag on Alt+Click (once per drag)
        if (!drag_triggered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            PLOG_INFO << "[Mini-Drag] Starting native window drag (Alt+Drag)";
#ifdef _WIN32
            SDL_Window* sdl_win = app.window();
            if (sdl_win)
            {
                HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_win), 
                                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
                if (hwnd)
                {
                    ReleaseCapture();
                    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
            }
#endif
            drag_triggered = true;
        }
    }
    
    // Reset drag trigger when mouse is released
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        drag_triggered = false;
    }
}

// ============================================================================
// Mini mode: Dockspace container setup
// ============================================================================

static ImGuiID setup_mini_mode_dockspace(WindowRegistry& registry)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    
    // Get background alpha from first dialog (if any) to match transparency
    float background_alpha = 1.0f;
    auto dialogs = registry.windowsByType(UIWindowType::Dialog);
    if (!dialogs.empty())
    {
        if (auto* dialog = dynamic_cast<DialogWindow*>(dialogs[0]))
        {
            background_alpha = dialog->state().ui_state().background_alpha;
        }
    }
    
    // Set window background to match dialog transparency
    ImGui::SetNextWindowBgAlpha(background_alpha);
    
    ImGuiWindowFlags container_flags = 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    
    ImGuiID dockspace_id = 0;
    if (ImGui::Begin("MiniContainer###MiniContainer", nullptr, container_flags))
    {
        ImGuiDockNodeFlags dock_flags = 
            ImGuiDockNodeFlags_NoSplit | 
            ImGuiDockNodeFlags_NoResize | 
            ImGuiDockNodeFlags_NoUndocking;
        
        dockspace_id = ImGui::GetID("DockSpace_MiniContainer");
        ImGui::DockSpace(dockspace_id, ImVec2(0, 0), dock_flags);
    }
    ImGui::End();
    
    return dockspace_id;
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
    
#ifdef _WIN32
    SetupUtf8Console();
#endif

    AppContext app;
    if (!app.initialize())
        return 1;

    SDL_SetLogOutputFunction(sdl_log_bridge, nullptr);
    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

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
            dockspace_id = setup_mini_mode_dockspace(registry);
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
            handle_mini_mode_alt_drag(app);

        // Handle transparent area clicks and context menu
        handle_transparent_area_click(io, registry, app);
        render_global_context_menu(io, registry, show_manager, quit_requested);
        
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
