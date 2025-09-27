#pragma once

#include "DialogState.hpp"

#include <vector>

struct SDL_Renderer;
struct ImGuiIO;

// FontManager manages a shared ImGui font atlas and propagates changes to dialog states.
class FontManager
{
public:
    FontManager(ImGuiIO& io);

    void registerDialog(DialogState& state);
    void unregisterDialog(DialogState& state);

    void ensureFont(DialogState& state);
    bool reloadFont(const char* path);

    ImFont* currentFont() const { return current_font_; }
    bool hasCustomFont() const { return has_custom_font_; }

private:
    void assignFontToDialogs(ImFont* font, bool custom);
    ImFont* loadFontFromPath(const char* path, bool& custom_loaded);
    ImFont* loadFallbackFont(bool& custom_loaded);

    ImGuiIO& io_;
    std::vector<DialogState*> dialogs_;
    ImFont* current_font_ = nullptr;
    bool has_custom_font_ = false;
};
