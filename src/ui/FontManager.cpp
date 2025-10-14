#include "FontManager.hpp"
#include "utils/CrashHandler.hpp"
#include "utils/ErrorReporter.hpp"

#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr float kDialogFontSize = 28.0f;
    constexpr std::array<const char*, 8> kFontCandidates = {
        "assets/fonts/NotoSansJP-Medium.ttf",
        "assets/fonts/NotoSansCJKjp-Medium.otf",
        "assets/fonts/NotoSansCJK-Medium.ttc",
        "assets/fonts/NotoSansCJKjp-Medium.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJKjp-Regular.otf",
        "/Library/Fonts/Arial Unicode.ttf",
        "C:/Windows/Fonts/msgothic.ttc"
    };

    std::once_flag g_default_font_once;

    std::string JoinList(const std::vector<std::string>& entries)
    {
        std::string result;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i)
                result += ", ";
            result += entries[i];
        }
        return result;
    }
}

// Prepares font storage tied to the ImGui IO context.
FontManager::FontManager()
{
}

// Keeps track of dialog states that need font updates.
void FontManager::registerDialog(UIState& state)
{
    dialogs_.push_back(&state);
    ensureFont(state);
}

// Removes a dialog state from update tracking.
void FontManager::unregisterDialog(UIState& state)
{
    dialogs_.erase(std::remove(dialogs_.begin(), dialogs_.end(), &state), dialogs_.end());
}

// Makes sure the dialog has an assigned font.
void FontManager::ensureFont(UIState& state)
{
    if (!current_font_)
    {
        bool custom = false;
        current_font_ = loadFallbackFont(custom);
        has_custom_font_ = custom;
        assignFontToDialogs(current_font_, has_custom_font_);
    }
    state.font = current_font_;
    state.has_custom_font = has_custom_font_;
    if (current_font_ && current_font_->LegacySize > 0.0f)
    {
        float previous_base = state.font_base_size;
        state.font_base_size = current_font_->LegacySize;
        if (previous_base > 0.0f)
            state.font_size = state.font_size / previous_base * state.font_base_size;
        else if (state.font_size <= 0.0f)
            state.font_size = state.font_base_size;
    }
}

// Attempts to reload the atlas with a user-provided font.
bool FontManager::reloadFont(const char* path)
{
    utils::CrashHandler::SetContext("FontManager::reloadFont");
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
    ImGui::GetIO().Fonts->ClearFonts();

    bool custom_loaded = false;
    current_font_ = loadFontFromPath(path, custom_loaded);
    if (!current_font_)
    {
        current_font_ = loadFallbackFont(custom_loaded);
    }

    assignFontToDialogs(current_font_, custom_loaded);
    ImGui_ImplSDLRenderer3_CreateDeviceObjects();
    return custom_loaded;
}

// Distributes the active font to all registered dialogs.
void FontManager::assignFontToDialogs(ImFont* font, bool custom)
{
    has_custom_font_ = custom;
    for (UIState* state : dialogs_)
    {
        if (!state)
            continue;
        state->font = font;
        state->has_custom_font = custom;
        if (font && font->LegacySize > 0.0f)
        {
            float previous_base = state->font_base_size;
            state->font_base_size = font->LegacySize;
            if (previous_base > 0.0f)
                state->font_size = state->font_size / previous_base * state->font_base_size;
            else if (state->font_size <= 0.0f)
                state->font_size = state->font_base_size;
        }
    }
}

// Loads a font without user-facing reporting; used by both direct and fallback loading paths.
ImFont* FontManager::tryLoadFont(const char* path, bool& custom_loaded)
{
    custom_loaded = false;
    if (!path || !path[0])
        return nullptr;

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 2;
    config.PixelSnapH  = false;

    ImGuiIO& io = ImGui::GetIO();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    ImFont* font = io.Fonts->AddFontFromFileTTF(path, kDialogFontSize, &config, ranges.Data);
    if (font)
    {
        custom_loaded = true;
        io.Fonts->Build();
        PLOG_INFO << "Loaded dialog font: " << path;
    }
    else
    {
        PLOG_WARNING << "Failed to load dialog font: " << path;
    }
    return font;
}

// Loads font from a specific path when available and reports failures to the user.
ImFont* FontManager::loadFontFromPath(const char* path, bool& custom_loaded)
{
    custom_loaded = false;
    if (!path || !path[0])
        return nullptr;

    if (!std::filesystem::exists(path))
    {
        PLOG_WARNING << "Font path not found: " << path;
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization,
            "Font file not found",
            std::string("Could not locate font at ") + path);
        return nullptr;
    }

    ImFont* font = tryLoadFont(path, custom_loaded);
    if (!font)
    {
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization,
            "Failed to load font",
            std::string("ImGui could not load font from ") + path);
    }
    return font;
}

// Picks the first available candidate or the default font as fallback.
ImFont* FontManager::loadFallbackFont(bool& custom_loaded)
{
    custom_loaded = false;
    std::vector<std::string> missing_paths;
    std::vector<std::string> failed_paths;

    for (const char* candidate : kFontCandidates)
    {
        if (!std::filesystem::exists(candidate))
        {
            missing_paths.emplace_back(candidate);
            PLOG_WARNING << "Font path not found: " << candidate;
            continue;
        }

        bool candidate_custom = false;
        ImFont* font = tryLoadFont(candidate, candidate_custom);
        if (font)
        {
            custom_loaded = candidate_custom;
            return font;
        }

        failed_paths.emplace_back(candidate);
    }

    PLOG_WARNING << "Using ImGui default font; CJK glyphs may be missing.";
    std::call_once(g_default_font_once, [missing_paths, failed_paths]{
        std::string details;
        if (!missing_paths.empty())
        {
            details += "Missing: " + JoinList(missing_paths);
        }
        if (!failed_paths.empty())
        {
            if (!details.empty())
                details += " | ";
            details += "Failed to load: " + JoinList(failed_paths);
        }
        if (details.empty())
        {
            details = "All bundled fonts failed to load; some glyphs may be missing.";
        }

        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization,
            "Using fallback font",
            details);
    });
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontDefault();
    io.Fonts->Build();
    custom_loaded = false;
    return font;
}
