#include <SDL3/SDL.h>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cfloat>

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

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::filesystem::create_directories("logs");
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::info, "logs/run.log");
    if (auto logger = plog::get()) logger->addAppender(&consoleAppender);
    SDL_SetLogOutputFunction(sdl_log_bridge, nullptr);

    SDL_SetAppMetadata("DQX Utility", "0.1.0", "https://github.com/K1ngst0m/dqx-utility");
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT;
    SDL_Window* window     = SDL_CreateWindow("DQX Utility", 800, 600, win_flags);
    if (!window)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& imgui_io = ImGui::GetIO();

    ImFont* dialog_font = imgui_io.Fonts->AddFontDefault();
    static ImVector<ImWchar> dialog_glyph_ranges;
    if (dialog_glyph_ranges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(imgui_io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(imgui_io.Fonts->GetGlyphRangesJapanese());
        builder.AddRanges(imgui_io.Fonts->GetGlyphRangesKorean());
        builder.AddRanges(imgui_io.Fonts->GetGlyphRangesChineseFull());
        builder.AddRanges(imgui_io.Fonts->GetGlyphRangesCyrillic());
        builder.BuildRanges(&dialog_glyph_ranges);
    }

    const std::array<const char*, 8> font_candidates = {
        "fonts/NotoSansJP-Regular.ttf",
        "fonts/NotoSansCJKjp-Regular.otf",
        "fonts/NotoSansCJK-Regular.ttc",
        "fonts/NotoSansCJKjp-Bold.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJKjp-Regular.otf",
        "/Library/Fonts/Arial Unicode.ttf",
        "C:/Windows/Fonts/msgothic.ttc"
    };

    const float dialog_font_size = 28.0f;
    char initial_font_path[512] = {0};
    for (const char* path : font_candidates)
    {
        if (!std::filesystem::exists(path))
            continue;

        ImFontConfig config;
        config.OversampleH = 3;
        config.OversampleV = 2;
        config.PixelSnapH  = false;
        ImFont* loaded = imgui_io.Fonts->AddFontFromFileTTF(path, dialog_font_size, &config, dialog_glyph_ranges.Data);
        if (loaded)
        {
            dialog_font = loaded;
            std::snprintf(initial_font_path, sizeof(initial_font_path), "%s", path);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded dialog font: %s", path);
            break;
        }
    }

    struct DialogUIState
    {
        float width;
        float vertical_ratio;
        ImVec2 padding;
        float rounding;
        float border_thickness;
        char title[128];
        char body[1024];
        bool show_title;
        char font_path[512];
        bool has_custom_font;
        ImVec2 window_pos;
        ImVec2 window_size;
        float height;
        bool height_initialized;
        bool pending_reposition;
        bool pending_resize;
    };

    DialogUIState dialog_state{};
    dialog_state.width            = 580.0f;
    dialog_state.vertical_ratio   = 0.75f;
    dialog_state.padding          = ImVec2(24.0f, 18.0f);
    dialog_state.rounding         = 16.0f;
    dialog_state.border_thickness = 2.0f;
    dialog_state.show_title       = true;
    dialog_state.has_custom_font  = initial_font_path[0] != '\0';
    std::snprintf(dialog_state.title, sizeof(dialog_state.title), "%s", reinterpret_cast<const char*>(u8"冒険ガイド"));
    std::snprintf(dialog_state.body, sizeof(dialog_state.body), "%s", reinterpret_cast<const char*>(u8"メインコマンド『せんれき』の\nこれまでのおはなしを見ながら\n物語を進めていこう。"));
    std::snprintf(dialog_state.font_path, sizeof(dialog_state.font_path), "%s", initial_font_path);

    bool show_settings_window = true;
    dialog_state.window_pos      = ImVec2(0.0f, 0.0f);
    dialog_state.window_size     = ImVec2(dialog_state.width, 220.0f);
    dialog_state.height          = dialog_state.window_size.y;
    dialog_state.height_initialized = true;
    dialog_state.pending_reposition = true;
    dialog_state.pending_resize  = true;
    dialog_state.has_custom_font = dialog_font != nullptr && !imgui_io.Fonts->Fonts.empty() && dialog_font != imgui_io.Fonts->Fonts[0];

    auto rebuild_dialog_font = [&](const char* path) -> bool {
        ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
        imgui_io.Fonts->ClearFonts();

        ImFont* default_font = imgui_io.Fonts->AddFontDefault();
        ImFont* new_dialog_font = default_font;
        bool loaded_custom = false;

        if (path && path[0])
        {
            if (std::filesystem::exists(path))
            {
                ImFontConfig config;
                config.OversampleH = 3;
                config.OversampleV = 2;
                config.PixelSnapH  = false;
                ImFont* loaded = imgui_io.Fonts->AddFontFromFileTTF(path, dialog_font_size, &config, dialog_glyph_ranges.Data);
                if (loaded)
                {
                    new_dialog_font = loaded;
                    loaded_custom = true;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded dialog font: %s", path);
                }
                else
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load dialog font: %s", path);
                }
            }
            else
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Font path not found: %s", path);
            }
        }

        dialog_font = new_dialog_font;
        dialog_state.has_custom_font = loaded_custom;
        ImGui_ImplSDLRenderer3_CreateDeviceObjects();
        return loaded_custom;
    };
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "ImGui_ImplSDL3_InitForSDLRenderer failed");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR, "ImGui_ImplSDLRenderer3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT)
                running = false;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const float max_dialog_width  = std::max(200.0f, imgui_io.DisplaySize.x - 40.0f);
        const float max_dialog_height = std::max(120.0f, imgui_io.DisplaySize.y - 40.0f);
        dialog_state.width = std::clamp(dialog_state.width, 200.0f, max_dialog_width);
        dialog_state.vertical_ratio = std::clamp(dialog_state.vertical_ratio, 0.1f, 0.9f);
        dialog_state.padding.x = std::clamp(dialog_state.padding.x, 4.0f, 80.0f);
        dialog_state.padding.y = std::clamp(dialog_state.padding.y, 4.0f, 80.0f);
        dialog_state.rounding = std::clamp(dialog_state.rounding, 0.0f, 32.0f);
        dialog_state.border_thickness = std::clamp(dialog_state.border_thickness, 0.5f, 6.0f);
        dialog_state.height = std::clamp(dialog_state.height, 80.0f, max_dialog_height);

        if (dialog_state.pending_reposition)
        {
            const ImVec2 anchor(imgui_io.DisplaySize.x * 0.5f, imgui_io.DisplaySize.y * dialog_state.vertical_ratio);
            ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        }
        else
        {
            ImGui::SetNextWindowPos(dialog_state.window_pos, ImGuiCond_Appearing);
        }

        if (dialog_state.pending_resize)
        {
            float desired_height = dialog_state.height;
            ImVec2 desired_size(dialog_state.width, desired_height);
            ImGui::SetNextWindowSize(desired_size, ImGuiCond_Always);
        }

        ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 80.0f), ImVec2(max_dialog_width, imgui_io.DisplaySize.y));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dialog_state.padding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, dialog_state.rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, dialog_state.border_thickness);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        const ImGuiWindowFlags dialog_flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("DQXDialog", nullptr, dialog_flags))
        {
            if (dialog_font)
                ImGui::PushFont(dialog_font);

            if (dialog_state.show_title && dialog_state.title[0] != '\0')
            {
                ImGui::TextUnformatted(dialog_state.title);
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
                ImGui::Separator();
                ImGui::PopStyleColor();
            }

            const float wrap_width = std::max(40.0f, dialog_state.width - dialog_state.padding.x * 2.0f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
            ImGui::TextUnformatted(dialog_state.body);
            ImGui::PopTextWrapPos();

            if (dialog_font)
                ImGui::PopFont();

            const bool was_pending_reposition = dialog_state.pending_reposition;
            const bool was_pending_resize     = dialog_state.pending_resize;

            dialog_state.window_pos  = ImGui::GetWindowPos();
            dialog_state.window_size = ImGui::GetWindowSize();

            if (!was_pending_resize)
            {
                dialog_state.width = dialog_state.window_size.x;
                dialog_state.height = dialog_state.window_size.y;
            }
            dialog_state.height_initialized = true;

            if (!was_pending_reposition)
            {
                const float win_center_y = dialog_state.window_pos.y + dialog_state.window_size.y * 0.5f;
                const float display_h    = std::max(1.0f, imgui_io.DisplaySize.y);
                dialog_state.vertical_ratio = std::clamp(win_center_y / display_h, 0.1f, 0.9f);
            }

            dialog_state.pending_reposition = false;
            dialog_state.pending_resize     = false;
        }
        ImGui::End();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        bool width_slider_changed = false;
        bool anchor_slider_changed = false;
        bool height_slider_changed = false;

        if (show_settings_window)
        {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(420.0f, 440.0f), ImGuiCond_FirstUseEver);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));

            const ImGuiWindowFlags settings_flags = ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::Begin("Dialog Settings", &show_settings_window, settings_flags))
            {
                ImGui::TextUnformatted("Dialog Settings");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Checkbox("Show Title", &dialog_state.show_title);
                ImGui::Spacing();

                ImFont* active_font = dialog_font;
                const bool push_font = (active_font != nullptr);
                if (push_font)
                    ImGui::PushFont(active_font);

                if (!dialog_state.show_title)
                    ImGui::BeginDisabled();
                ImGui::TextUnformatted("Title Text");
                {
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(std::max(220.0f, avail));
                }
                ImGui::InputText("##dialog_title", dialog_state.title, IM_ARRAYSIZE(dialog_state.title));
                if (!dialog_state.show_title)
                    ImGui::EndDisabled();
                ImGui::Spacing();

                if (push_font)
                    ImGui::PopFont();

                if (push_font)
                    ImGui::PushFont(active_font);
                ImGui::TextUnformatted("Body Text");
                {
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(std::max(220.0f, avail));
                }
                ImGui::InputTextMultiline("##dialog_body", dialog_state.body, IM_ARRAYSIZE(dialog_state.body), ImVec2(-FLT_MIN, 140.0f));
                if (push_font)
                    ImGui::PopFont();
                ImGui::Spacing();

                auto set_slider_width = [&]() {
                    const float label_reserve = 140.0f;
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(std::max(140.0f, avail - label_reserve));
                };

                ImGui::TextUnformatted("Dialog Width");
                set_slider_width();
                width_slider_changed |= ImGui::SliderFloat("##dialog_width", &dialog_state.width, 200.0f, max_dialog_width);
                ImGui::Spacing();

                ImGui::TextUnformatted("Dialog Height");
                set_slider_width();
                height_slider_changed |= ImGui::SliderFloat("##dialog_height", &dialog_state.height, 80.0f, max_dialog_height);
                ImGui::Spacing();

                ImGui::TextUnformatted("Vertical Anchor");
                set_slider_width();
                anchor_slider_changed |= ImGui::SliderFloat("##dialog_anchor", &dialog_state.vertical_ratio, 0.1f, 0.9f);
                ImGui::Spacing();

                ImGui::TextUnformatted("Padding XY");
                set_slider_width();
                ImGui::SliderFloat2("##dialog_padding", &dialog_state.padding.x, 4.0f, 80.0f);
                ImGui::Spacing();

                ImGui::TextUnformatted("Corner Rounding");
                set_slider_width();
                ImGui::SliderFloat("##dialog_rounding", &dialog_state.rounding, 0.0f, 32.0f);
                ImGui::Spacing();

                ImGui::TextUnformatted("Border Thickness");
                set_slider_width();
                ImGui::SliderFloat("##dialog_border", &dialog_state.border_thickness, 0.5f, 6.0f);

                ImGui::Spacing();

                ImGui::TextUnformatted("Font Path");
                float avail = ImGui::GetContentRegionAvail().x;
                ImGui::SetNextItemWidth(std::max(220.0f, avail - 120.0f));
                ImGui::InputText("##dialog_font_path", dialog_state.font_path, IM_ARRAYSIZE(dialog_state.font_path));
                ImGui::SameLine();
                if (ImGui::Button("Reload Font"))
                    dialog_state.has_custom_font = rebuild_dialog_font(dialog_state.font_path);

                ImGui::TextDisabled("Active font: %s", dialog_state.has_custom_font ? "custom" : "default (ASCII only)");

                if (!dialog_state.has_custom_font)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "No CJK font loaded; Japanese text may appear as '?' characters.");
            }
            ImGui::End();

            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(3);
        }

        if (width_slider_changed)
        {
            dialog_state.window_size.x = dialog_state.width;
            dialog_state.pending_resize = true;
        }
        if (height_slider_changed)
        {
            dialog_state.window_size.y = dialog_state.height;
            dialog_state.pending_resize = true;
        }

        if (anchor_slider_changed)
            dialog_state.pending_reposition = true;

        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
