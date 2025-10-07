#include "AppContext.hpp"
#include "FontManager.hpp"
#include "SettingsPanel.hpp"
#include "WindowRegistry.hpp"
#include "DialogWindow.hpp"
#include "config/ConfigManager.hpp"
#include "utils/ErrorReporter.hpp"
#include "utils/CrashHandler.hpp"
#include "ErrorDialog.hpp"
#include "ui/Localization.hpp"

#include <SDL3/SDL.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>

#include <filesystem>
#include <fstream>
#include <toml++/toml.h>
#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <clocale>
#include <fcntl.h>
#include <io.h>
static void SetupUtf8Console()
{
    // Set Windows console to UTF-8 so Japanese text displays correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Enable UTF-8 mode for console output
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            SetConsoleMode(hOut, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
        }
    }
    
    // Set C locale to UTF-8
    std::setlocale(LC_ALL, ".UTF-8");
}
#endif

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

// Check if mouse is outside all dialog windows
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
            bool within_dialog = ImGui::IsMouseHoveringRect(state.ui_state().window_pos,
                ImVec2(state.ui_state().window_pos.x + state.ui_state().window_size.x,
                       state.ui_state().window_pos.y + state.ui_state().window_size.y), false);
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

static void render_global_context_menu(ImGuiIO& io, WindowRegistry& registry, bool& show_manager)
{
    if (is_mouse_outside_dialogs(io, registry) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("GlobalContextMenu");
    }

    if (ImGui::BeginPopup("GlobalContextMenu"))
    {
        if (ImGui::MenuItem(i18n::get("menu.global_settings")))
        {
            if (!show_manager)
                show_manager = true;
        }
        
        ImGui::EndPopup();
    }
}


int main(int argc, char** argv)
{
    utils::CrashHandler::Initialize();
    
    std::filesystem::create_directories("logs");
    
    // Check config file for append_logs setting (parse without ImGui)
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
    
    // Check command-line override for append_logs
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--append-logs") == 0) {
            append_logs = true;
        }
    }
    
    // Do not delete prior logs; keep appending to preserve crash context across runs
    
    // Initialize logging with console and file output
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;
    plog::init(plog::info, "logs/run.log");
    if (auto logger = plog::get())
        logger->addAppender(&console_appender);

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

    // Init config manager and bind to registry
    static ConfigManager cfg_mgr;
    ConfigManager_Set(&cfg_mgr);
    cfg_mgr.setRegistry(&registry);

    // Load config at startup; if no dialogs loaded, create a default one
    cfg_mgr.loadAtStartup();

    // Initialize GUI localization with global setting (default "en")
    i18n::init(cfg_mgr.getUILanguageCode());
    // Always start with bordered window regardless of config, and sync the flag for UI
    cfg_mgr.setBorderlessWindows(false);
    app.setWindowBorderless(false);
    if (registry.windowsByType(UIWindowType::Dialog).empty())
        registry.createDialogWindow();

    SettingsPanel settings_panel(registry);
    ErrorDialog error_dialog;

    bool show_manager = true;

    Uint64 last_time = SDL_GetTicks();
    bool running = true;
    // Track last-applied borderless state to detect changes
    bool last_borderless = cfg_mgr.getBorderlessWindows();

    while (running)
    {
        Uint64 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        last_time = current_time;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (app.processEvent(event))
            {
                running = false;
                break;
            }
        }

        if (!running)
            break;

        app.updateVignette(delta_time);

        // Apply borderless changes live if toggled in settings
        bool current_borderless = cfg_mgr.getBorderlessWindows();
        if (current_borderless != last_borderless)
        {
            app.setWindowBorderless(current_borderless);
            last_borderless = current_borderless;
        }

        app.beginFrame();

        ImGui::DockSpaceOverViewport(
            0,
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode
        );

        for (auto& window : registry.windows())
        {
            if (window)
                window->render(io);
        }

        // Process any dialog windows marked for removal
        registry.processRemovals();

        handle_transparent_area_click(io, registry, app);
        render_global_context_menu(io, registry, show_manager);
        if (show_manager)
            settings_panel.render(show_manager);

        // Check for pending errors and display
        if (utils::ErrorReporter::HasPendingErrors())
        {
            auto errors = utils::ErrorReporter::GetPendingErrors();
            error_dialog.Show(errors);
        }

        // Render error dialog (returns true if should exit for fatal error)
        if (error_dialog.Render())
        {
            running = false;
        }

        app.renderVignette();
        app.endFrame();
        SDL_Delay(16);
    }

    // Save config on quit
    if (auto* cm = ConfigManager_Get()) cm->saveAll();
    return 0;
}
