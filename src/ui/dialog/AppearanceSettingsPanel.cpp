#include "AppearanceSettingsPanel.hpp"

#include <imgui.h>
#include "../../state/DialogStateManager.hpp"
#include "../Localization.hpp"
#include "../UITheme.hpp"

AppearanceSettingsPanel::AppearanceSettingsPanel(DialogStateManager& state)
    : state_(state)
{
}

AppearanceSettingsPanel::RenderResult AppearanceSettingsPanel::render()
{
    RenderResult result;
    
    ImGuiIO& io = ImGui::GetIO();
    const float max_dialog_width  = std::max(200.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(120.0f, io.DisplaySize.y - 40.0f);
    
    auto set_slider_width = []() {
        const float label_reserve = 140.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(std::max(140.0f, avail - label_reserve));
    };
    
    ImGui::Checkbox(i18n::get("dialog.appearance.auto_scroll"), &state_.ui_state().auto_scroll_to_new);
    ImGui::Spacing();
    
    ImGui::TextUnformatted(i18n::get("dialog.appearance.width"));
    set_slider_width();
    result.width_changed = ImGui::SliderFloat("##dialog_width_slider", &state_.ui_state().width, 200.0f, max_dialog_width);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.height"));
    set_slider_width();
    result.height_changed = ImGui::SliderFloat("##dialog_height_slider", &state_.ui_state().height, 80.0f, max_dialog_height);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.padding_xy"));
    set_slider_width();
    ImGui::SliderFloat2("##dialog_padding_slider", &state_.ui_state().padding.x, 4.0f, 80.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.corner_rounding"));
    set_slider_width();
    ImGui::SliderFloat("##dialog_rounding_slider", &state_.ui_state().rounding, 0.0f, 32.0f);
    ImGui::Spacing();

    ImGui::Checkbox(i18n::get("dialog.appearance.border_enabled"), &state_.ui_state().border_enabled);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.dark_border_size"));
    set_slider_width();
    ImGui::SliderFloat("##dialog_vignette_thickness", &state_.ui_state().vignette_thickness, 0.0f, 100.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.background_opacity"));
    set_slider_width();
    result.alpha_changed = ImGui::SliderFloat("##dialog_bg_alpha_slider", &state_.ui_state().background_alpha, 0.0f, 1.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.appearance.font_size"));
    set_slider_width();
    float min_font = std::max(8.0f, state_.ui_state().font_base_size * 0.5f);
    float max_font = state_.ui_state().font_base_size * 2.5f;
    result.font_changed = ImGui::SliderFloat("##dialog_font_size_slider", &state_.ui_state().font_size, min_font, max_font);
    ImGui::Spacing();
    
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(i18n::get("dialog.appearance.fade.label"));
    bool fade_enabled_changed = ImGui::Checkbox(i18n::get("dialog.appearance.fade.enabled"), &state_.ui_state().fade_enabled);
    if (fade_enabled_changed)
    {
        state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
        state_.ui_state().current_alpha_multiplier = 1.0f;
    }

    if (state_.ui_state().fade_enabled)
    {
        ImGui::TextUnformatted(i18n::get("dialog.appearance.fade.timeout"));
        set_slider_width();
        if (ImGui::SliderFloat("##fade_timeout_slider", &state_.ui_state().fade_timeout, 5.0f, 120.0f, "%.0fs"))
        {
            state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
            state_.ui_state().current_alpha_multiplier = 1.0f;
        }
        ImGui::TextColored(UITheme::disabledColor(), "%s", i18n::get("dialog.appearance.fade.hint"));
    }
    
    return result;
}
