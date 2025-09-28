#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "IconUtils.hpp"

namespace
{
    constexpr ImVec4 kDialogBgColor      = ImVec4(0.0f, 0.0f, 0.0f, 0.78f);
    constexpr ImVec4 kDialogBorderColor  = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    constexpr ImVec4 kDialogTextColor    = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr ImVec4 kDialogSeparator    = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    constexpr ImVec4 kWarningColor       = ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
}

// Creates a dialog window instance with default state values.
DialogWindow::DialogWindow(FontManager& font_manager, ImGuiIO& io, int instance_id, const std::string& name)
    : font_manager_(font_manager)
{
    (void)io;

    name_ = name;
    id_suffix_ = "dialog_window_" + std::to_string(instance_id);
    settings_id_suffix_ = "dialog_settings_" + std::to_string(instance_id);
    overlay_id_suffix_ = "dialog_overlay_" + std::to_string(instance_id);
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;

    std::snprintf(state_.title.data(), state_.title.size(), "%s", reinterpret_cast<const char*>(u8"冒険ガイド"));
    std::snprintf(state_.body.data(), state_.body.size(), "%s", reinterpret_cast<const char*>(u8"メインコマンド『せんれき』の\nこれまでのおはなしを見ながら\n物語を進めていこう。"));
    state_.font_path.fill('\0');

    font_manager_.registerDialog(state_);
}

// Ensures the dialog state is unregistered when destroyed.
DialogWindow::~DialogWindow()
{
    font_manager_.unregisterDialog(state_);
}

// Renders the dialog preview each frame.
void DialogWindow::render(ImGuiIO& io)
{
    renderDialog(io);
    renderDialogOverlay();
    renderSettingsWindow(io);
}

// Renders the per-instance settings UI.
void DialogWindow::renderSettings(ImGuiIO& io)
{
    renderSettingsPanel(io);
}

// Internal helper for drawing the dialog window.
void DialogWindow::renderDialog(ImGuiIO& io)
{
    const float max_dialog_width  = std::max(200.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(120.0f, io.DisplaySize.y - 40.0f);

    state_.width  = std::clamp(state_.width, 200.0f, max_dialog_width);
    state_.height = std::clamp(state_.height, 80.0f, max_dialog_height);
    state_.padding.x        = std::clamp(state_.padding.x, 4.0f, 80.0f);
    state_.padding.y        = std::clamp(state_.padding.y, 4.0f, 80.0f);
    state_.rounding         = std::clamp(state_.rounding, 0.0f, 32.0f);
    state_.border_thickness = std::clamp(state_.border_thickness, 0.5f, 6.0f);

    if (state_.pending_reposition)
    {
        const ImVec2 anchor(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.75f);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    else
    {
        ImGui::SetNextWindowPos(state_.window_pos, ImGuiCond_Appearing);
    }

    if (state_.pending_resize)
    {
        ImGui::SetNextWindowSize(ImVec2(state_.width, state_.height), ImGuiCond_Always);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 80.0f), ImVec2(max_dialog_width, io.DisplaySize.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, state_.padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, state_.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, state_.border_thickness);
    ImVec4 dialog_bg = kDialogBgColor;
    dialog_bg.w = state_.background_alpha;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, dialog_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, kDialogBorderColor);
    ImGui::PushStyleColor(ImGuiCol_Text, kDialogTextColor);

    const ImGuiWindowFlags dialog_flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(window_label_.c_str(), nullptr, dialog_flags))
    {
        ImFont* active_font = state_.font;
        float font_scale = 1.0f;
        if (active_font && state_.font_base_size > 0.0f)
            font_scale = std::max(0.3f, state_.font_size / state_.font_base_size);
        if (active_font)
        {
            ImGui::PushFont(active_font);
            ImGui::SetWindowFontScale(font_scale);
        }

        if (state_.show_title && state_.title[0] != '\0')
        {
            ImGui::TextUnformatted(state_.title.data());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, kDialogSeparator);
            ImGui::Separator();
            ImGui::PopStyleColor();
        }

        const float wrap_width = std::max(40.0f, state_.width - state_.padding.x * 2.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
        ImGui::TextUnformatted(state_.body.data());
        ImGui::PopTextWrapPos();

        if (active_font)
        {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopFont();
        }

        const bool was_pending_resize = state_.pending_resize;

        state_.window_pos  = ImGui::GetWindowPos();
        state_.window_size = ImGui::GetWindowSize();

        if (!was_pending_resize)
        {
            state_.width  = state_.window_size.x;
            state_.height = state_.window_size.y;
        }

        state_.pending_reposition = false;
        state_.pending_resize     = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);

}

// Internal helper for drawing the settings controls.
void DialogWindow::renderSettingsPanel(ImGuiIO& io)
{
    ImGui::TextUnformatted("Show Title");
    ImGui::Checkbox("##dialog_show_title", &state_.show_title);
    ImGui::Spacing();

    const bool push_font = state_.font != nullptr;
    if (push_font)
        ImGui::PushFont(state_.font);

    if (!state_.show_title)
        ImGui::BeginDisabled();
    ImGui::TextUnformatted("Title Text");
    ImGui::InputText("##dialog_title_input", state_.title.data(), state_.title.size());
    if (!state_.show_title)
        ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::TextUnformatted("Body Text");
    ImGui::InputTextMultiline("##dialog_body_input", state_.body.data(), state_.body.size(), ImVec2(-FLT_MIN, 140.0f));

    if (push_font)
        ImGui::PopFont();

    ImGui::Spacing();

    const float max_dialog_width  = std::max(200.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(120.0f, io.DisplaySize.y - 40.0f);

    auto set_slider_width = []() {
        const float label_reserve = 140.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(std::max(140.0f, avail - label_reserve));
    };

    bool width_changed   = false;
    bool height_changed  = false;
    bool alpha_changed   = false;
    bool font_changed    = false;

    ImGui::TextUnformatted("Dialog Width");
    set_slider_width();
    width_changed = ImGui::SliderFloat("##dialog_width_slider", &state_.width, 200.0f, max_dialog_width);
    ImGui::Spacing();

    ImGui::TextUnformatted("Dialog Height");
    set_slider_width();
    height_changed = ImGui::SliderFloat("##dialog_height_slider", &state_.height, 80.0f, max_dialog_height);
    ImGui::Spacing();

    ImGui::TextUnformatted("Padding XY");
    set_slider_width();
    ImGui::SliderFloat2("##dialog_padding_slider", &state_.padding.x, 4.0f, 80.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted("Corner Rounding");
    set_slider_width();
    ImGui::SliderFloat("##dialog_rounding_slider", &state_.rounding, 0.0f, 32.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted("Border Thickness");
    set_slider_width();
    ImGui::SliderFloat("##dialog_border_slider", &state_.border_thickness, 0.5f, 6.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted("Background Opacity");
    set_slider_width();
    alpha_changed = ImGui::SliderFloat("##dialog_bg_alpha_slider", &state_.background_alpha, 0.0f, 1.0f);
    ImGui::Spacing();

    ImGui::TextUnformatted("Font Size");
    set_slider_width();
    float min_font = std::max(8.0f, state_.font_base_size * 0.5f);
    float max_font = state_.font_base_size * 2.5f;
    font_changed = ImGui::SliderFloat("##dialog_font_size_slider", &state_.font_size, min_font, max_font);
    ImGui::Spacing();

    ImGui::TextUnformatted("Font Path");
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(220.0f, avail - 120.0f));
    ImGui::InputText("##dialog_font_path_input", state_.font_path.data(), state_.font_path.size());
    ImGui::SameLine();
    if (ImGui::Button("Reload Font"))
    {
        bool loaded = font_manager_.reloadFont(state_.font_path.data());
        state_.has_custom_font = loaded;
    }

    ImGui::TextDisabled("Active font: %s", state_.has_custom_font ? "custom" : "default (ASCII only)");
    if (!state_.has_custom_font)
        ImGui::TextColored(kWarningColor, "No CJK font loaded; Japanese text may appear as '?' characters.");

    if (width_changed)
    {
        state_.window_size.x   = state_.width;
        state_.pending_resize  = true;
    }
    if (height_changed)
    {
        state_.window_size.y   = state_.height;
        state_.pending_resize  = true;
    }
    if (alpha_changed || font_changed)
        state_.pending_resize = state_.pending_resize;
}

// Draws an overlay icon when the dialog is hovered.
void DialogWindow::renderDialogOverlay()
{
    ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!ImGui::IsMousePosValid(&mouse))
        return;

    const ImVec2 icon_size(28.0f, 28.0f);
    const ImVec2 icon_offset(-icon_size.x - 16.0f, 16.0f);
    const ImVec2 icon_pos(state_.window_pos.x + state_.window_size.x + icon_offset.x,
                          state_.window_pos.y + icon_offset.y);

    bool hovering_dialog = ImGui::IsMouseHoveringRect(state_.window_pos,
        ImVec2(state_.window_pos.x + state_.window_size.x,
               state_.window_pos.y + state_.window_size.y), false);

    float target_visibility = hovering_dialog ? 0.5f : 0.0f;
    float delta = target_visibility - overlay_visibility_;
    overlay_visibility_ += delta * ImGui::GetIO().DeltaTime * 12.0f;
    overlay_visibility_ = std::clamp(overlay_visibility_, 0.0f, 1.0f);
    if (overlay_visibility_ <= 0.01f)
        return;

    ImGui::SetNextWindowPos(icon_pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    std::string overlay_label = "SettingsIcon###" + overlay_id_suffix_;
    if (ImGui::Begin(overlay_label.c_str(), nullptr, flags))
    {
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton("##dialog_settings_toggle", icon_size);
        bool hovered = ImGui::IsItemHovered();
        if (pressed)
            show_settings_window_ = !show_settings_window_;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 center(cursor.x + icon_size.x * 0.5f, cursor.y + icon_size.y * 0.5f);
        float combined_visibility = hovered ? 1.0f : overlay_visibility_;
        DrawMenuIcon(draw_list, center, icon_size.x * 0.5f, combined_visibility, hovered);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// Presents the per-instance settings window when requested.
void DialogWindow::renderSettingsWindow(ImGuiIO& io)
{
    if (!show_settings_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(settings_window_label_.c_str(), &show_settings_window_))
    {
        renderSettingsPanel(io);
    }
    ImGui::End();
}

// Updates display names and ImGui labels after rename.
void DialogWindow::rename(const char* new_name)
{
    if (!new_name || !new_name[0])
        return;

    name_ = new_name;
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
}
