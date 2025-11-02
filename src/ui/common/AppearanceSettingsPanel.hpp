#pragma once

struct BaseWindowState;

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

    explicit AppearanceSettingsPanel(BaseWindowState& state);

    RenderResult render();

private:
    BaseWindowState& state_;
};
