#pragma once

#include "UIState.hpp"
#include "TranslationConfig.hpp"
#include "ContentState.hpp"

struct DialogStateManager
{
    UIState ui;
    TranslationConfig translation;
    ContentState content;
    bool use_global_translation = true;

    UIState& ui_state() { return ui; }
    const UIState& ui_state() const { return ui; }

    TranslationConfig& translation_config() { return translation; }
    const TranslationConfig& translation_config() const { return translation; }

    ContentState& content_state() { return content; }
    const ContentState& content_state() const { return content; }

    void applyDefaults()
    {
        ui.applyDefaults();
        translation.applyDefaults();
        content.applyDefaults();
        use_global_translation = true;
    }
};
