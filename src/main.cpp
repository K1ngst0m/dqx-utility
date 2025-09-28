#include "AppContext.hpp"
#include "FontManager.hpp"
#include "SettingsPanel.hpp"
#include "WindowRegistry.hpp"
#include "DialogWindow.hpp"
#include "IconUtils.hpp"
#include "config/ConfigManager.hpp"

#include <SDL3/SDL.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>

#include <filesystem>

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
            bool within_dialog = ImGui::IsMouseHoveringRect(state.window_pos,
                ImVec2(state.window_pos.x + state.window_size.x,
                       state.window_pos.y + state.window_size.y), false);
            if (within_dialog)
                return false;
        }
    }
    return true;
}

// Handle global right-click context menu
static void render_global_context_menu(ImGuiIO& io, WindowRegistry& registry, bool& show_manager)
{
    // Open global context menu on right-click outside all dialog windows
    if (is_mouse_outside_dialogs(io, registry) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("GlobalContextMenu");
    }

    // Render the global context menu
    if (ImGui::BeginPopup("GlobalContextMenu"))
    {
        if (ImGui::MenuItem("Window Settings"))
        {
            if (!show_manager)  // Only open if not already open
                show_manager = true;
        }
        
        ImGui::EndPopup();
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::filesystem::create_directories("logs");
    static plog::ConsoleAppender<plog::TxtFormatter> console_appender;
    plog::init(plog::info, "logs/run.log");
    if (auto logger = plog::get())
        logger->addAppender(&console_appender);

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
    if (registry.windowsByType(UIWindowType::Dialog).empty())
        registry.createDialogWindow();

    SettingsPanel settings_panel(registry);

    bool show_manager = true;

    bool running = true;
    while (running)
    {
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

        app.beginFrame();

        for (auto& window : registry.windows())
        {
            if (window)
                window->render(io);
        }

        // Process any dialog windows marked for removal
        registry.processRemovals();

        render_global_context_menu(io, registry, show_manager);
        if (show_manager)
            settings_panel.render(show_manager);

        app.endFrame();
        SDL_Delay(16);
    }

    // Save config on quit
    if (auto* cm = ConfigManager_Get()) cm->saveAll();
    return 0;
}
