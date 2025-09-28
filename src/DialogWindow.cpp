#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "IconUtils.hpp"
#include "ipc/TextSourceClient.hpp"

namespace
{
    constexpr ImVec4 kDialogBgColor           = ImVec4(0.0f, 0.0f, 0.0f, 0.78f);
    constexpr ImVec4 kDialogBorderColor       = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    constexpr ImVec4 kDialogTextColor         = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    constexpr ImVec4 kDialogSeparator         = ImVec4(1.0f, 1.0f, 1.0f, 0.92f);
    constexpr ImVec4 kWarningColor            = ImVec4(1.0f, 0.6f, 0.4f, 1.0f);
    constexpr float  kDialogSeparatorThickness = 3.0f;
    constexpr float  kDialogSeparatorSpacing   = 6.0f;
}

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

    state_.font_path.fill('\0');
    state_.append_buffer.fill('\0');
    state_.segments.emplace_back();
    std::snprintf(state_.segments.back().data(), state_.segments.back().size(), "%s", reinterpret_cast<const char*>(u8"メインコマンド『せんれき』の\nこれまでのおはなしを見ながら\n物語を進めていこう。"));
    state_.portfile_path.fill('\0');
    std::snprintf(state_.portfile_path.data(), state_.portfile_path.size(), "%s", "../dqxc/ipc.port");

    font_manager_.registerDialog(state_);
}

DialogWindow::~DialogWindow()
{
    font_manager_.unregisterDialog(state_);
    if (client_)
        client_->disconnect();
}

void DialogWindow::applyPending()
{
    // Pull from client inbox first
    if (client_ && client_->isConnected())
    {
        std::vector<ipc::Incoming> msgs;
        if (client_->poll(msgs))
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            for (auto& m : msgs)
            {
                if (m.type == "dialog")
                {
                    PendingMsg pm; pm.text = std::move(m.text); pm.lang = std::move(m.lang); pm.seq = m.seq;
                    pending_.push_back(std::move(pm));
                }
            }
        }
    }

    std::vector<PendingMsg> local;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local.swap(pending_);
    }
    if (local.empty())
        return;
    appended_since_last_frame_ = true;
    for (auto& m : local)
    {
        state_.segments.emplace_back();
        std::snprintf(state_.segments.back().data(), state_.segments.back().size(), "%s", m.text.c_str());
        if (m.seq > 0)
        {
            last_applied_seq_ = m.seq;
            if (client_)
                client_->sendAck(last_applied_seq_);
        }
    }
}

void DialogWindow::render(ImGuiIO& io)
{
    appended_since_last_frame_ = false;
    applyPending();
    renderDialog(io);
    renderDialogOverlay();
    renderSettingsWindow(io);
}

// Renders the per-instance settings UI.
void DialogWindow::renderSettings(ImGuiIO& io)
{
    renderSettingsPanel(io);
}

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

        const float wrap_width = std::max(40.0f, state_.width - state_.padding.x * 2.0f);
        for (size_t i = 0; i < state_.segments.size(); ++i)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
            ImGui::TextUnformatted(state_.segments[i].data());
            ImGui::PopTextWrapPos();
            if (i + 1 < state_.segments.size())
            {
                ImGui::Dummy(ImVec2(0.0f, kDialogSeparatorSpacing));
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 win_pos = ImGui::GetWindowPos();
                ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
                ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
                float x1 = win_pos.x + cr_min.x;
                float x2 = win_pos.x + cr_max.x;
                float y  = ImGui::GetCursorScreenPos().y;
                draw_list->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + kDialogSeparatorThickness), ImGui::GetColorU32(kDialogSeparator));
                ImGui::Dummy(ImVec2(0.0f, kDialogSeparatorSpacing + kDialogSeparatorThickness));
            }
        }

        if (active_font)
        {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopFont();
        }

        // Auto-scroll to bottom when new content is appended
        if (state_.auto_scroll_to_new && appended_since_last_frame_)
        {
            ImGui::SetScrollHereY(1.0f);
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

void DialogWindow::renderSettingsPanel(ImGuiIO& io)
{
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

    ImGui::Separator();
    ImGui::TextDisabled("Text Source (IPC)");

    ImGui::Checkbox("Auto-scroll to new", &state_.auto_scroll_to_new);

    ImGui::TextUnformatted("Portfile Path");
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        float avail = ImGui::GetContentRegionAvail().x;
        float btn_w = ImGui::CalcTextSize("Connect").x + style.FramePadding.x * 2.0f + ImGui::CalcTextSize("Disconnect").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(220.0f, avail - btn_w - style.ItemSpacing.x));
        ImGui::InputText("##portfile_path", state_.portfile_path.data(), state_.portfile_path.size());
        ImGui::SameLine();
        bool connected = client_ && client_->isConnected();
        if (!connected)
        {
            if (ImGui::Button("Connect"))
            {
                if (!client_) client_ = std::make_unique<ipc::TextSourceClient>();
                last_error_.fill('\0');
                if (!client_->connectFromPortfile(state_.portfile_path.data()))
                {
                    std::snprintf(last_error_.data(), last_error_.size(), "%s", client_->lastError());
                }
            }
        }
        else
        {
            if (ImGui::Button("Disconnect"))
            {
                client_->disconnect();
            }
        }
        ImGui::TextDisabled("Status: %s", (client_ && client_->isConnected()) ? "Connected" : "Disconnected");
        if (last_error_[0] != '\0')
            ImGui::TextColored(kWarningColor, "%s", last_error_.data());
    }

    ImGui::Separator();
    ImGui::TextDisabled("Debug Session");

    ImGui::PushID(settings_id_suffix_.c_str());

    ImGui::Spacing();
    ImGui::TextUnformatted("Font Path");
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        float avail = ImGui::GetContentRegionAvail().x;
        float btn_w = ImGui::CalcTextSize("Reload Font").x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(std::max(220.0f, avail - btn_w - style.ItemSpacing.x));
        ImGui::InputText("##font_path", state_.font_path.data(), state_.font_path.size());
        ImGui::SameLine();
        if (ImGui::Button("Reload Font"))
        {
            bool loaded = font_manager_.reloadFont(state_.font_path.data());
            state_.has_custom_font = loaded;
        }
        ImGui::TextDisabled("Active font: %s", state_.has_custom_font ? "custom" : "default (ASCII only)");
        if (!state_.has_custom_font)
            ImGui::TextColored(kWarningColor, "No CJK font loaded; Japanese text may appear as '?' characters.");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Appended Texts");

    // Wrap list in a child region to ensure proper clipping
    if (ImGui::BeginChild("SegmentsChild", ImVec2(0, 220.0f), ImGuiChildFlags_Border))
    {
        int to_delete = -1;
        for (int i = 0; i < static_cast<int>(state_.segments.size()); ++i)
        {
            ImGui::PushID(i);
            const ImGuiStyle& style = ImGui::GetStyle();
            float row_avail = ImGui::GetContentRegionAvail().x;
            // Reserve space for Edit and Delete buttons
            float edit_w = ImGui::CalcTextSize("Edit").x + style.FramePadding.x * 2.0f;
            float del_w  = ImGui::CalcTextSize("Delete").x + style.FramePadding.x * 2.0f;
            float text_w = std::max(220.0f, row_avail - edit_w - del_w - style.ItemSpacing.x * 2.0f);

            // Render single-line with ellipsis trimming
            {
                ImGui::BeginGroup();
                ImVec2 start = ImGui::GetCursorScreenPos();
                ImVec2 line_size(text_w, ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f);
                ImGui::InvisibleButton("##line", line_size);
                ImVec2 clip_min = start;
                ImVec2 clip_max = ImVec2(start.x + text_w, start.y + line_size.y);
                ImGui::PushClipRect(clip_min, clip_max, true);

                const char* full = state_.segments[i].data();
                std::string display = full;
                ImVec2 full_sz = ImGui::CalcTextSize(display.c_str());
                if (full_sz.x > text_w)
                {
                    std::string ell = display;
                    const char* ellipsis = "...";
                    // Trim until it fits
                    while (!ell.empty())
                    {
                        ImVec2 sz = ImGui::CalcTextSize((ell + ellipsis).c_str());
                        if (sz.x <= text_w)
                        {
                            display = ell + ellipsis;
                            break;
                        }
                        ell.pop_back();
                    }
                    if (ell.empty())
                        display = ellipsis; // fallback
                }
                ImGui::SetCursorScreenPos(ImVec2(start.x + style.FramePadding.x, start.y + style.FramePadding.y));
                ImGui::TextUnformatted(display.c_str());
                ImGui::PopClipRect();
                ImGui::EndGroup();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
            {
                state_.editing_index = i;
                std::snprintf(state_.edit_buffer.data(), state_.edit_buffer.size(), "%s", state_.segments[i].data());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
                to_delete = i;
            ImGui::PopID();
        }
        if (to_delete >= 0 && to_delete < static_cast<int>(state_.segments.size()))
            state_.segments.erase(state_.segments.begin() + to_delete);
    }
    ImGui::EndChild();

    // Full editor for selected entry
    if (state_.editing_index >= 0 && state_.editing_index < static_cast<int>(state_.segments.size()))
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Editing Entry #%d", state_.editing_index);
        ImVec2 box(0, 160.0f);
        ImGui::InputTextMultiline("##full_editor", state_.edit_buffer.data(), state_.edit_buffer.size(), box);
        if (ImGui::Button("Save"))
        {
            // Save back to segment (truncate to buffer size)
            std::snprintf(state_.segments[state_.editing_index].data(), state_.segments[state_.editing_index].size(), "%s", state_.edit_buffer.data());
            state_.editing_index = -1;
            state_.edit_buffer[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            state_.editing_index = -1;
            state_.edit_buffer[0] = '\0';
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Append New Text");
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        float append_avail = ImGui::GetContentRegionAvail().x;
        float btn_w = ImGui::CalcTextSize("Append").x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(std::max(220.0f, append_avail - btn_w - style.ItemSpacing.x));
        ImGui::InputText("##append", state_.append_buffer.data(), state_.append_buffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Append"))
        {
            if (state_.append_buffer[0] != '\0')
            {
                state_.segments.emplace_back();
                std::snprintf(state_.segments.back().data(), state_.segments.back().size(), "%s", state_.append_buffer.data());
                state_.append_buffer[0] = '\0';
            }
        }
    }

    ImGui::PopID();


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

void DialogWindow::renderSettingsWindow(ImGuiIO& io)
{
    if (!show_settings_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
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
