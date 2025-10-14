#pragma once

#include "state/UIState.hpp"

#include <vector>

// FontManager manages a shared ImGui font atlas and propagates changes to dialog states.
class FontManager
{
public:
    FontManager();

    void registerDialog(UIState& state);
    void unregisterDialog(UIState& state);

    void ensureFont(UIState& state);
    bool reloadFont(const char* path);

    ImFont* currentFont() const { return current_font_; }
    bool hasCustomFont() const { return has_custom_font_; }

private:
    void assignFontToDialogs(ImFont* font, bool custom);
    ImFont* tryLoadFont(const char* path, bool& custom_loaded);
    ImFont* loadFontFromPath(const char* path, bool& custom_loaded);
    ImFont* loadFallbackFont(bool& custom_loaded);

    std::vector<UIState*> dialogs_;
    ImFont* current_font_ = nullptr;
    bool has_custom_font_ = false;
};
