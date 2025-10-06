#include "FontManager.hpp"
#include "utils/CrashHandler.hpp"

#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <array>
#include <filesystem>

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
}

// Prepares font storage tied to the ImGui IO context.
FontManager::FontManager(ImGuiIO& io)
    : io_(io)
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
    io_.Fonts->ClearFonts();

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

// Loads font from a specific path when available.
ImFont* FontManager::loadFontFromPath(const char* path, bool& custom_loaded)
{
    custom_loaded = false;
    if (!path || !path[0])
        return nullptr;

    if (!std::filesystem::exists(path))
    {
        PLOG_WARNING << "Font path not found: " << path;
        return nullptr;
    }

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 2;
    config.PixelSnapH  = false;

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io_.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io_.Fonts->GetGlyphRangesJapanese());
    builder.AddRanges(io_.Fonts->GetGlyphRangesKorean());
    builder.AddRanges(io_.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io_.Fonts->GetGlyphRangesCyrillic());
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    ImFont* font = io_.Fonts->AddFontFromFileTTF(path, kDialogFontSize, &config, ranges.Data);
    if (font)
    {
        custom_loaded = true;
        PLOG_INFO << "Loaded dialog font: " << path;
    }
    else
    {
        PLOG_WARNING << "Failed to load dialog font: " << path;
    }
    return font;
}

// Picks the first available candidate or the default font as fallback.
ImFont* FontManager::loadFallbackFont(bool& custom_loaded)
{
    custom_loaded = false;
    for (const char* candidate : kFontCandidates)
    {
        ImFont* font = loadFontFromPath(candidate, custom_loaded);
        if (font)
            return font;
    }

    PLOG_WARNING << "Using ImGui default font; CJK glyphs may be missing.";
    ImFont* font = io_.Fonts->AddFontDefault();
    custom_loaded = false;
    return font;
}
