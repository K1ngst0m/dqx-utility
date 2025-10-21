#pragma once

struct DialogStateManager;

class AppearanceSettingsPanel
{
public:
    struct RenderResult
    {
        bool width_changed = false;
        bool height_changed = false;
        bool alpha_changed = false;
        bool font_changed = false;
    };

    explicit AppearanceSettingsPanel(DialogStateManager& state);

    RenderResult render();

private:
    DialogStateManager& state_;
};
