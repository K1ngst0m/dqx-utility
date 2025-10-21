#include "DebugSettingsPanel.hpp"

#include <cstring>
#include <imgui.h>
#include "../../state/DialogStateManager.hpp"
#include "../FontManager.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../Localization.hpp"
#include "../UITheme.hpp"

DebugSettingsPanel::DebugSettingsPanel(DialogStateManager& state, FontManager& fontManager, TranslateSession& session)
    : state_(state)
    , fontManager_(fontManager)
    , session_(session)
{
}

void DebugSettingsPanel::render(const std::string& settingsIdSuffix)
{
    ImGui::PushID(settingsIdSuffix.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    renderFontSection();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    renderCacheSection();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted(i18n::get("dialog.settings.appended_texts"));
    if (ImGui::BeginChild("SegmentsChild", ImVec2(0, 220.0f), ImGuiChildFlags_Border))
    {
        renderSegmentList();
    }
    ImGui::EndChild();

    renderSegmentEditor();
    renderNewSegmentInput();

    ImGui::PopID();
}

void DebugSettingsPanel::renderFontSection()
{
    ImGui::TextUnformatted(i18n::get("dialog.settings.font_path"));

    const ImGuiStyle& style = ImGui::GetStyle();
    float avail = ImGui::GetContentRegionAvail().x;
    float btn_w = ImGui::CalcTextSize(i18n::get("dialog.settings.reload_font")).x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(220.0f, avail - btn_w - style.ItemSpacing.x));
    ImGui::InputText("##font_path", state_.ui_state().font_path.data(), state_.ui_state().font_path.size());
    ImGui::SameLine();
    if (ImGui::Button(i18n::get("dialog.settings.reload_font")))
    {
        bool loaded = fontManager_.reloadFont(state_.ui_state().font_path.data());
        state_.ui_state().has_custom_font = loaded;
    }
    ImGui::TextDisabled("%s %s", i18n::get("dialog.settings.font_active_label"),
                        state_.ui_state().has_custom_font ? i18n::get("dialog.settings.font_active_custom") :
                                                            i18n::get("dialog.settings.font_active_default"));
    if (!state_.ui_state().has_custom_font)
    {
        ImGui::TextColored(UITheme::warningColor(), "%s", i18n::get("dialog.settings.font_warning_no_cjk"));
    }
}

void DebugSettingsPanel::renderCacheSection()
{
    ImGui::TextUnformatted(i18n::get("dialog.settings.translation_cache"));

    {
        std::string t =
            i18n::format("dialog.settings.cache_entries", {
                                                              { "cur", std::to_string(session_.cacheEntries())  },
                                                              { "cap", std::to_string(session_.cacheCapacity()) }
        });
        ImGui::TextUnformatted(t.c_str());
    }
    {
        std::string t =
            i18n::format("dialog.settings.cache_hits",
                         {
                             { "n", std::to_string(static_cast<unsigned long long>(session_.cacheHits())) }
        });
        ImGui::TextUnformatted(t.c_str());
    }
    {
        std::string t =
            i18n::format("dialog.settings.cache_misses",
                         {
                             { "n", std::to_string(static_cast<unsigned long long>(session_.cacheMisses())) }
        });
        ImGui::TextUnformatted(t.c_str());
    }

    bool cache_enabled = session_.isCacheEnabled();
    if (ImGui::Checkbox(i18n::get("dialog.settings.enable_cache"), &cache_enabled))
    {
        session_.enableCache(cache_enabled);
    }

    if (ImGui::Button(i18n::get("dialog.settings.clear_cache")))
    {
        session_.clear();
    }
}

void DebugSettingsPanel::renderSegmentList()
{
    int to_delete = -1;
    for (int i = 0; i < static_cast<int>(state_.content_state().segments.size()); ++i)
    {
        ImGui::PushID(i);
        const ImGuiStyle& style = ImGui::GetStyle();
        float row_avail = ImGui::GetContentRegionAvail().x;
        float edit_w = ImGui::CalcTextSize(i18n::get("dialog.append.edit")).x + style.FramePadding.x * 2.0f;
        float del_w = ImGui::CalcTextSize(i18n::get("dialog.append.delete")).x + style.FramePadding.x * 2.0f;
        float text_w = std::max(220.0f, row_avail - edit_w - del_w - style.ItemSpacing.x * 2.0f);

        {
            ImGui::BeginGroup();
            ImVec2 start = ImGui::GetCursorScreenPos();
            ImVec2 line_size(text_w, ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f);
            ImGui::InvisibleButton("##line", line_size);
            ImVec2 clip_min = start;
            ImVec2 clip_max = ImVec2(start.x + text_w, start.y + line_size.y);
            ImGui::PushClipRect(clip_min, clip_max, true);

            const char* full = state_.content_state().segments[i].data();
            std::string display = full;
            ImVec2 full_sz = ImGui::CalcTextSize(display.c_str());
            if (full_sz.x > text_w)
            {
                std::string ell = display;
                const char* ellipsis = "...";
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
                    display = ellipsis;
            }
            ImGui::SetCursorScreenPos(ImVec2(start.x + style.FramePadding.x, start.y + style.FramePadding.y));
            ImGui::TextUnformatted(display.c_str());
            ImGui::PopClipRect();
            ImGui::EndGroup();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(i18n::get("dialog.append.edit")))
        {
            state_.content_state().editing_index = i;
            std::snprintf(state_.content_state().edit_buffer.data(), state_.content_state().edit_buffer.size(), "%s",
                          state_.content_state().segments[i].data());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(i18n::get("dialog.append.delete")))
            to_delete = i;
        ImGui::PopID();
    }

    if (to_delete >= 0 && to_delete < static_cast<int>(state_.content_state().segments.size()))
    {
        state_.content_state().segments.erase(state_.content_state().segments.begin() + to_delete);
        if (to_delete < static_cast<int>(state_.content_state().speakers.size()))
            state_.content_state().speakers.erase(state_.content_state().speakers.begin() + to_delete);
    }
}

void DebugSettingsPanel::renderSegmentEditor()
{
    if (state_.content_state().editing_index >= 0 &&
        state_.content_state().editing_index < static_cast<int>(state_.content_state().segments.size()))
    {
        ImGui::Spacing();
        {
            std::string t = i18n::format("dialog.append.editing_entry",
                                         {
                                             { "index", std::to_string(state_.content_state().editing_index) }
            });
            ImGui::TextDisabled("%s", t.c_str());
        }
        ImVec2 box(0, 160.0f);
        ImGui::InputTextMultiline("##full_editor", state_.content_state().edit_buffer.data(),
                                  state_.content_state().edit_buffer.size(), box);
        if (ImGui::Button(i18n::get("common.save")))
        {
            auto& seg = state_.content_state().segments[state_.content_state().editing_index];
            std::size_t n = std::min(seg.size() - 1, std::strlen(state_.content_state().edit_buffer.data()));
            std::memcpy(seg.data(), state_.content_state().edit_buffer.data(), n);
            seg[n] = '\0';
            state_.content_state().editing_index = -1;
            state_.content_state().edit_buffer[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button(i18n::get("common.cancel")))
        {
            state_.content_state().editing_index = -1;
            state_.content_state().edit_buffer[0] = '\0';
        }
    }
}

void DebugSettingsPanel::renderNewSegmentInput()
{
    ImGui::Spacing();
    ImGui::TextUnformatted(i18n::get("dialog.append.new_text"));

    const ImGuiStyle& style = ImGui::GetStyle();
    float append_avail = ImGui::GetContentRegionAvail().x;
    float btn_w = ImGui::CalcTextSize(i18n::get("dialog.append.append_button")).x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(220.0f, append_avail - btn_w - style.ItemSpacing.x));
    ImGui::InputText("##append", state_.content_state().append_buffer.data(),
                     state_.content_state().append_buffer.size());
    ImGui::SameLine();
    if (ImGui::Button(i18n::get("dialog.append.append_button")))
    {
        if (state_.content_state().append_buffer[0] != '\0')
        {
            state_.content_state().segments.emplace_back();
            state_.content_state().speakers.emplace_back();
            auto& back = state_.content_state().segments.back();
            std::size_t n = std::min(back.size() - 1, std::strlen(state_.content_state().append_buffer.data()));
            std::memcpy(back.data(), state_.content_state().append_buffer.data(), n);
            back[n] = '\0';
            state_.content_state().append_buffer[0] = '\0';
        }
    }
}
