#include "AppContext.hpp"
#include "FontManager.hpp"
#include "SettingsPanel.hpp"
#include "WindowRegistry.hpp"
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

// Renders the floating settings toggle icon over the application window.
static void render_settings_toggle(ImGuiIO& io, bool& show_manager)
{
    static float visibility = 0.0f;
    const bool mouse_valid = ImGui::IsMousePosValid(&io.MousePos);
    const bool inside_window = mouse_valid && io.MousePos.x >= 0.0f && io.MousePos.x <= io.DisplaySize.x &&
        io.MousePos.y >= 0.0f && io.MousePos.y <= io.DisplaySize.y;

    float target_visibility = inside_window ? 0.5f : 0.0f;
    float delta = target_visibility - visibility;
    visibility += delta * io.DeltaTime * 10.0f;
    visibility = std::clamp(visibility, 0.0f, 1.0f);
    if (visibility < 0.02f)
        return;

    const ImVec2 icon_size(36.0f, 36.0f);
    const ImVec2 icon_pos(io.DisplaySize.x - icon_size.x - 24.0f, 24.0f);

    ImGui::SetNextWindowPos(icon_pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("SettingsToggle##app", nullptr, flags))
    {
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton("##menu_toggle", icon_size);
        bool hovered = ImGui::IsItemHovered();
        if (pressed)
            show_manager = !show_manager;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 center(cursor.x + icon_size.x * 0.5f, cursor.y + icon_size.y * 0.5f);
        float target = hovered ? 1.0f : (inside_window ? 0.5f : 0.0f);
        float local_delta = target - visibility;
        visibility += local_delta * io.DeltaTime * 12.0f;
        visibility = std::clamp(visibility, 0.0f, 1.0f);
        DrawMenuIcon(draw_list, center, icon_size.x * 0.5f, visibility, hovered);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
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

        render_settings_toggle(io, show_manager);
        if (show_manager)
            settings_panel.render(show_manager);

        app.endFrame();
        SDL_Delay(16);
    }

    // Save config on quit
    if (auto* cm = ConfigManager_Get()) cm->saveAll();
    return 0;
}
