#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

#include "translate/ITranslator.hpp"
#include "translate/LabelProcessor.hpp"
#include "config/ConfigManager.hpp"
#include "UITheme.hpp"
#include "DQXClarityService.hpp"
#include "DQXClarityLauncher.hpp"
#include "dqxclarity/api/dialog_message.hpp"
#include "ui/Localization.hpp"

namespace
{
    // Prevents UTF-8 character corruption when copying to fixed-size buffers
    // Walks backwards from truncation point to find safe character boundary
    void safe_copy_utf8(char* dest, size_t dest_size, const std::string& src)
    {
        if (dest_size == 0) return;
        if (src.empty()) { dest[0] = '\0'; return; }
        
        size_t copy_len = std::min(src.length(), dest_size - 1);
        
        // If we're truncating, find a safe UTF-8 boundary
        if (copy_len < src.length())
        {
            // Walk backwards to find the start of the last complete UTF-8 character
            while (copy_len > 0 && (src[copy_len] & 0x80) && !(src[copy_len] & 0x40))
            {
                --copy_len;
            }
        }
        
        std::memcpy(dest, src.c_str(), copy_len);
        dest[copy_len] = '\0';
    }

    const char* waiting_text_for_lang(TranslationConfig::TargetLang lang)
    {
        switch (lang)
        {
        case TranslationConfig::TargetLang::EN_US: return "Waiting";
        case TranslationConfig::TargetLang::ZH_CN: return "等待中";
        case TranslationConfig::TargetLang::ZH_TW: return "等待中";
        }
        return "Waiting";
    }

    const char* dots_for_phase(int phase)
    {
        switch (phase % 4)
        {
        case 0: return ".";
        case 1: return "..";
        case 2: return "...";
        case 3: return "..";
        }
        return ".";
    }
}


DialogWindow::DialogWindow(FontManager& font_manager, ImGuiIO& io, int instance_id, const std::string& name)
    : font_manager_(font_manager)
{
    (void)io;

    name_ = name;
    id_suffix_ = "dialog_window_" + std::to_string(instance_id);
    settings_id_suffix_ = "dialog_settings_" + std::to_string(instance_id);
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
    
    label_processor_ = std::make_unique<LabelProcessor>();

    // Initialize all state to defaults
    state_.applyDefaults();

    // Register with font manager for UI rendering
    font_manager_.registerDialog(state_.ui_state());
}

DialogWindow::~DialogWindow()
{
    font_manager_.unregisterDialog(state_.ui_state());
}


void DialogWindow::refreshFontBinding()
{
    // Re-assign active font and base size after external state replacement (e.g., config load)
    font_manager_.ensureFont(state_.ui_state());
}

void DialogWindow::applyPending()
{
    // Filter incoming messages to prevent empty translation requests
    auto is_blank = [](const std::string& s) {
        for (char c : s) { if (!std::isspace(static_cast<unsigned char>(c))) return false; }
        return true;
    };

    // Pull new dialog messages from in-process backlog
    if (auto* launcher = DQXClarityService_Get())
    {
        std::vector<dqxclarity::DialogMessage> msgs;
        if (launcher->copyDialogsSince(last_applied_seq_, msgs))
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            for (auto& m : msgs)
            {
                if (!m.text.empty() && !is_blank(m.text))
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
        if (state_.translation_config().translate_enabled)
        {
            // Process labels before translation
            std::string processed_text = label_processor_->processText(m.text);

            auto submit = session_.submit(
                processed_text,
                state_.translation_config().translation_backend,
                state_.translation_config().target_lang_enum,
                translator_.get());

            if (submit.kind == TranslateSession::SubmitKind::Cached)
            {
                state_.content_state().segments.emplace_back();
                safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), submit.text.c_str());
                if (m.seq > 0) last_applied_seq_ = m.seq;
                continue;
            }

            std::uint64_t job_id = submit.job_id;
            bool show_placeholder = false;
            if (submit.kind == TranslateSession::SubmitKind::Queued && job_id != 0)
            {
                last_job_id_ = job_id;
                show_placeholder = true;
            }
            else if (submit.kind == TranslateSession::SubmitKind::DroppedNotReady)
            {
                show_placeholder = true;
            }

            if (show_placeholder)
            {
                state_.content_state().segments.emplace_back();
                auto& buf = state_.content_state().segments.back();
                std::string placeholder = std::string(waiting_text_for_lang(state_.translation_config().target_lang_enum)) + " .";
                safe_copy_utf8(buf.data(), buf.size(), placeholder);
                if (job_id != 0)
                {
                    pending_segment_by_job_[job_id] = static_cast<int>(state_.content_state().segments.size()) - 1;
                }
            }

            if (m.seq > 0) last_applied_seq_ = m.seq;
        }
        else
        {
            state_.content_state().segments.emplace_back();
            safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), m.text);
        }
        if (m.seq > 0)
        {
            last_applied_seq_ = m.seq;
            // No ack needed in in-process mode
        }
    }
}

void DialogWindow::render(ImGuiIO& io)
{
    appended_since_last_frame_ = false;
    applyPending();

    if (auto* cm = ConfigManager_Get())
        cm->pollAndApply();

    // Process completed translations from background worker
    if (translator_)
    {
        std::vector<translate::Completed> done;
        if (translator_->drain(done))
        {
            appended_since_last_frame_ = true;
            std::vector<TranslateSession::CompletedEvent> events;
            session_.onCompleted(done, events);
            for (auto& ev : events)
            {
                auto it = pending_segment_by_job_.find(ev.job_id);
                if (it != pending_segment_by_job_.end())
                {
                    int idx = it->second;
                    if (idx >= 0 && idx < static_cast<int>(state_.content_state().segments.size()))
                    {
                        safe_copy_utf8(state_.content_state().segments[idx].data(), state_.content_state().segments[idx].size(), ev.text);
                    }
                    pending_segment_by_job_.erase(it);
                }
                else
                {
                    state_.content_state().segments.emplace_back();
                    safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), ev.text);
                }
            }
        }
    }

    renderDialog(io);
    renderDialogContextMenu();
    renderSettingsWindow(io);
}

// Renders the per-instance settings UI.
void DialogWindow::renderSettings(ImGuiIO& io)
{
    // If config manager recently reported a parse error from manual edits, surface it here
    if (auto* cm = ConfigManager_Get())
    {
        if (const char* err = cm->lastError(); err && err[0])
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err);
        }
    }
    renderSettingsPanel(io);
}

void DialogWindow::renderDialog(ImGuiIO& io)
{
    const float max_dialog_width  = std::max(200.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(120.0f, io.DisplaySize.y - 40.0f);

    state_.ui_state().width  = std::clamp(state_.ui_state().width, 200.0f, max_dialog_width);
    state_.ui_state().height = std::clamp(state_.ui_state().height, 80.0f, max_dialog_height);
    state_.ui_state().padding.x        = std::clamp(state_.ui_state().padding.x, 4.0f, 80.0f);
    state_.ui_state().padding.y        = std::clamp(state_.ui_state().padding.y, 4.0f, 80.0f);
    state_.ui_state().rounding         = std::clamp(state_.ui_state().rounding, 0.0f, 32.0f);
    state_.ui_state().border_thickness = std::clamp(state_.ui_state().border_thickness, 0.5f, 6.0f);

    if (state_.ui_state().pending_reposition)
    {
        const ImVec2 anchor(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.75f);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    else
    {
        ImGui::SetNextWindowPos(state_.ui_state().window_pos, ImGuiCond_Appearing);
    }

    if (state_.ui_state().pending_resize)
    {
        ImGui::SetNextWindowSize(ImVec2(state_.ui_state().width, state_.ui_state().height), ImGuiCond_Always);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 80.0f), ImVec2(max_dialog_width, io.DisplaySize.y));

    UITheme::pushDialogStyle(state_.ui_state().background_alpha, state_.ui_state().padding, state_.ui_state().rounding, state_.ui_state().border_thickness);

    const ImGuiWindowFlags dialog_flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(window_label_.c_str(), nullptr, dialog_flags))
    {
        // Soft vignette inside the dialog with rounded corners, no overlaps
        {
            float thickness = std::max(0.0f, state_.ui_state().vignette_thickness);
            if (thickness > 0.0f)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 win_pos = ImGui::GetWindowPos();
                ImVec2 win_size = ImGui::GetWindowSize();
                float rounding0 = std::max(0.0f, state_.ui_state().rounding);

                // Feather steps scale with thickness (capped for perf)
                int steps = static_cast<int>(std::ceil(thickness * 3.0f));
                steps = std::clamp(steps, 1, 256);

                // Increase overall darkness as size grows
                float max_alpha = std::clamp(0.30f + 0.006f * thickness, 0.30f, 0.65f);

                for (int i = 0; i < steps; ++i)
                {
                    float t = (steps <= 1) ? 0.0f : (static_cast<float>(i) / (steps - 1));
                    float inset = t * thickness;
                    ImVec2 pmin(win_pos.x + inset, win_pos.y + inset);
                    ImVec2 pmax(win_pos.x + win_size.x - inset, win_pos.y + win_size.y - inset);
                    float r = std::max(0.0f, rounding0 - inset);
                    // Smooth fade curve (ease-out), slightly stronger
                    float a = max_alpha * (1.0f - t);
                    a = a * a; // quadratic ease-out
                    if (a <= 0.0f) continue;
                    ImU32 col = IM_COL32(0, 0, 0, (int)(a * 255.0f));
                    dl->AddRect(pmin, pmax, col, r, 0, 1.0f);
                }
            }
        }

        ImFont* active_font = state_.ui_state().font;
        float font_scale = 1.0f;
        if (active_font && state_.ui_state().font_base_size > 0.0f)
            font_scale = std::max(0.3f, state_.ui_state().font_size / state_.ui_state().font_base_size);
        if (active_font)
        {
            ImGui::PushFont(active_font);
            ImGui::SetWindowFontScale(font_scale);
        }

        const float wrap_width = std::max(40.0f, state_.ui_state().width - state_.ui_state().padding.x * 2.0f);

        // Update 'Waiting...' placeholder animation for in-flight translations
        if (!pending_segment_by_job_.empty())
        {
            waiting_anim_accum_ += io.DeltaTime;
            const float step = 0.35f; // seconds per phase
            while (waiting_anim_accum_ >= step)
            {
                waiting_anim_accum_ -= step;
                waiting_anim_phase_ = (waiting_anim_phase_ + 1) % 4; // ., .., ..., ..
            }
            std::string base = waiting_text_for_lang(state_.translation_config().target_lang_enum);
            std::string suffix = " "; suffix += dots_for_phase(waiting_anim_phase_);
            for (const auto& kv : pending_segment_by_job_)
            {
                int idx = kv.second;
                if (idx >= 0 && idx < static_cast<int>(state_.content_state().segments.size()))
                {
                    std::string composed = base + suffix;
                    safe_copy_utf8(state_.content_state().segments[idx].data(), state_.content_state().segments[idx].size(), composed);
                }
            }
        }

        for (size_t i = 0; i < state_.content_state().segments.size(); ++i)
        {
            // Draw outlined text: render 8 shadow passes around the text, then the main text on top.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImFont* font_to_use = ImGui::GetFont();
            float font_size_px = ImGui::GetFontSize();
            const char* txt = state_.content_state().segments[i].data();
            float wrap_w = wrap_width;

            // Colors
            ImVec4 text_col_v4 = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 outline_col = IM_COL32(0, 0, 0, (int)(text_col_v4.w * 255.0f));

            // Outline thickness scales slightly with font size (clamped)
            float thickness = std::clamp(std::round(font_size_px * 0.06f), 1.0f, 3.0f);

            // 8-directional outline
            for (int ox = -1; ox <= 1; ++ox)
            {
                for (int oy = -1; oy <= 1; ++oy)
                {
                    if (ox == 0 && oy == 0) continue;
                    dl->AddText(font_to_use, font_size_px, ImVec2(pos.x + ox * thickness, pos.y + oy * thickness), outline_col, txt, nullptr, wrap_w);
                }
            }
            // Main text fill
            dl->AddText(font_to_use, font_size_px, pos, text_col, txt, nullptr, wrap_w);

            // Advance layout by the wrapped text height
            ImVec2 text_sz = ImGui::CalcTextSize(txt, nullptr, false, wrap_w);
            ImGui::Dummy(ImVec2(0.0f, text_sz.y));
            if (i + 1 < state_.content_state().segments.size())
            {
                ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing()));
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 win_pos = ImGui::GetWindowPos();
                ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
                ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
                float x1 = win_pos.x + cr_min.x;
                float x2 = win_pos.x + cr_max.x;
                float y  = ImGui::GetCursorScreenPos().y;
                draw_list->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + UITheme::dialogSeparatorThickness()), ImGui::GetColorU32(UITheme::dialogSeparatorColor()));
                ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing() + UITheme::dialogSeparatorThickness()));
            }
        }

        if (active_font)
        {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopFont();
        }

        // Smooth, constant-speed auto-scroll to bottom when content grows
        if (state_.ui_state().auto_scroll_to_new)
        {
            const float curr_scroll = ImGui::GetScrollY();
            const float curr_max    = ImGui::GetScrollMaxY();

            // Initialize tracking on first layout
            if (!scroll_initialized_)
            {
                last_scroll_max_y_ = curr_max;
                scroll_initialized_ = true;
            }

            // If content height increased since last frame and user was at (or near) bottom, start animating
            const bool content_grew   = (curr_max > last_scroll_max_y_ + 0.5f);
            const bool was_at_bottom  = (last_scroll_max_y_ <= 0.5f) || ((last_scroll_max_y_ - curr_scroll) <= 2.0f);
            if (!scroll_animating_ && content_grew && was_at_bottom)
            {
                scroll_animating_ = true;
            }

            // Advance animation at constant speed until we reach the bottom
            if (scroll_animating_)
            {
                const float target = curr_max;
                const float current = ImGui::GetScrollY();
                float delta = target - current;
                const float step = SCROLL_SPEED * io.DeltaTime;

                if (std::fabs(delta) <= step)
                {
                    ImGui::SetScrollY(target);
                    scroll_animating_ = false;
                }
                else
                {
                    ImGui::SetScrollY(current + (delta > 0.0f ? step : -step));
                }
            }

            // Update for next frame comparison
            last_scroll_max_y_ = curr_max;
        }

        const bool was_pending_resize = state_.ui_state().pending_resize;

        state_.ui_state().window_pos  = ImGui::GetWindowPos();
        state_.ui_state().window_size = ImGui::GetWindowSize();

        if (!was_pending_resize)
        {
            state_.ui_state().width  = state_.ui_state().window_size.x;
            state_.ui_state().height = state_.ui_state().window_size.y;
        }

        state_.ui_state().pending_reposition = false;
        state_.ui_state().pending_resize     = false;


    }
    ImGui::End();

    UITheme::popDialogStyle();
}

void DialogWindow::initTranslatorIfEnabled()
{
    if (!state_.translation_config().translate_enabled)
    {
        if (translator_) { translator_->shutdown(); translator_.reset(); }
        return;
    }
    translate::TranslatorConfig cfg;
    cfg.backend = static_cast<translate::Backend>(state_.translation_config().translation_backend);
    switch (state_.translation_config().target_lang_enum)
    {
    case TranslationConfig::TargetLang::EN_US: cfg.target_lang = "en-us"; break;
    case TranslationConfig::TargetLang::ZH_CN: cfg.target_lang = "zh-cn"; break;
    case TranslationConfig::TargetLang::ZH_TW: cfg.target_lang = "zh-tw"; break;
    }
    if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
    {
        cfg.base_url = state_.translation_config().openai_base_url.data();
        cfg.model = state_.translation_config().openai_model.data();
        cfg.api_key = state_.translation_config().openai_api_key.data();
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
    {
        cfg.base_url.clear();
        cfg.model.clear();
        cfg.api_key = state_.translation_config().google_api_key.data();
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
    {
        cfg.base_url = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
        cfg.model = "glm-4-flash";
        cfg.api_key = state_.translation_config().zhipu_api_key.data();
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::QwenMT)
    {
        cfg.base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
        const char* qm = state_.translation_config().qwen_model.data();
        cfg.model = (qm && qm[0]) ? qm : "qwen-mt-turbo";
        cfg.api_key = state_.translation_config().qwen_api_key.data();
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Niutrans)
    {
        cfg.base_url = "https://api.niutrans.com/NiuTransServer/translation";
        cfg.model.clear();
        cfg.api_key = state_.translation_config().niutrans_api_key.data();
    }
    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg))
    {
        translator_.reset();
    }
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

    // Config save button at the top
    if (ImGui::Button(i18n::get("dialog.settings.save_config")))
    {
        extern bool ConfigManager_SaveAll();
        bool ok = ConfigManager_SaveAll();
        if (!ok)
        {
            ImGui::SameLine();
            ImGui::TextColored(UITheme::warningColor(), "%s", i18n::get("dialog.settings.save_config_failed"));
        }
    }
    ImGui::Spacing();

    // APPEARANCE SECTION
    if (ImGui::CollapsingHeader(i18n::get("dialog.appearance.title")))
    {
        ImGui::Indent();
        
        ImGui::Checkbox(i18n::get("dialog.appearance.auto_scroll"), &state_.ui_state().auto_scroll_to_new);
        ImGui::Spacing();
        
        ImGui::TextUnformatted(i18n::get("dialog.appearance.width"));
        set_slider_width();
        width_changed = ImGui::SliderFloat("##dialog_width_slider", &state_.ui_state().width, 200.0f, max_dialog_width);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.height"));
        set_slider_width();
        height_changed = ImGui::SliderFloat("##dialog_height_slider", &state_.ui_state().height, 80.0f, max_dialog_height);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.padding_xy"));
        set_slider_width();
        ImGui::SliderFloat2("##dialog_padding_slider", &state_.ui_state().padding.x, 4.0f, 80.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.corner_rounding"));
        set_slider_width();
        ImGui::SliderFloat("##dialog_rounding_slider", &state_.ui_state().rounding, 0.0f, 32.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.border_thickness"));
        set_slider_width();
        ImGui::SliderFloat("##dialog_border_slider", &state_.ui_state().border_thickness, 0.5f, 6.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.dark_border_size"));
        set_slider_width();
        ImGui::SliderFloat("##dialog_vignette_thickness", &state_.ui_state().vignette_thickness, 0.0f, 100.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.background_opacity"));
        set_slider_width();
        alpha_changed = ImGui::SliderFloat("##dialog_bg_alpha_slider", &state_.ui_state().background_alpha, 0.0f, 1.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted(i18n::get("dialog.appearance.font_size"));
        set_slider_width();
        float min_font = std::max(8.0f, state_.ui_state().font_base_size * 0.5f);
        float max_font = state_.ui_state().font_base_size * 2.5f;
        font_changed = ImGui::SliderFloat("##dialog_font_size_slider", &state_.ui_state().font_size, min_font, max_font);
        
        ImGui::Unindent();
        ImGui::Spacing();
    }

    // TRANSLATE SECTION
    if (ImGui::CollapsingHeader(i18n::get("dialog.translate.title"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        
        bool enable_changed = ImGui::Checkbox(i18n::get("dialog.translate.enable"), &state_.translation_config().translate_enabled);
        bool auto_apply_changed = ImGui::Checkbox(i18n::get("dialog.translate.auto_apply"), &state_.translation_config().auto_apply_changes);
        ImGui::Spacing();
        
        ImGui::TextUnformatted(i18n::get("dialog.translate.backend.label"));
        const char* backend_items[] = {
            i18n::get("dialog.translate.backend.items.openai_compat"),
            i18n::get("dialog.translate.backend.items.google"),
            i18n::get("dialog.translate.backend.items.glm4_zhipu"),
            i18n::get("dialog.translate.backend.items.qwen_mt"),
            i18n::get("dialog.translate.backend.items.niutrans")
        };
        int current_backend = static_cast<int>(state_.translation_config().translation_backend);
        ImGui::SetNextItemWidth(220.0f);
        bool backend_changed = ImGui::Combo("##translation_backend", &current_backend, backend_items, IM_ARRAYSIZE(backend_items));
        if (backend_changed)
        {
            state_.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(current_backend);
        }
        
        ImGui::TextUnformatted(i18n::get("dialog.settings.target_language"));
        const char* lang_items[] = {
            i18n::get("dialog.settings.target_lang.en_us"),
            i18n::get("dialog.settings.target_lang.zh_cn"),
            i18n::get("dialog.settings.target_lang.zh_tw")
        };
        int current_lang = static_cast<int>(state_.translation_config().target_lang_enum);
        ImGui::SetNextItemWidth(220.0f);
        bool lang_changed = ImGui::Combo("##target_lang", &current_lang, lang_items, IM_ARRAYSIZE(lang_items));
        if (lang_changed)
        {
            state_.translation_config().target_lang_enum = static_cast<TranslationConfig::TargetLang>(current_lang);
        }

        // Show backend-specific configuration
        bool base_url_changed = false;
        bool model_changed = false;
        bool openai_key_changed = false;
        bool google_key_changed = false;
        bool zhipu_key_changed = false;
        bool qwen_key_changed = false;
        bool niutrans_key_changed = false;
        
        if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
        {
            ImGui::TextUnformatted(i18n::get("dialog.settings.base_url"));
            ImGui::SetNextItemWidth(300.0f);
            base_url_changed = ImGui::InputText("##openai_base", state_.translation_config().openai_base_url.data(), state_.translation_config().openai_base_url.size());

            ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
            ImGui::SetNextItemWidth(300.0f);
            model_changed = ImGui::InputText("##openai_model", state_.translation_config().openai_model.data(), state_.translation_config().openai_model.size());

            ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
            ImGui::SetNextItemWidth(300.0f);
            openai_key_changed = ImGui::InputText("##openai_key", state_.translation_config().openai_api_key.data(), state_.translation_config().openai_api_key.size(), ImGuiInputTextFlags_Password);
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
        {
            ImGui::TextUnformatted(i18n::get("dialog.settings.api_key_optional"));
            ImGui::SetNextItemWidth(300.0f);
            google_key_changed = ImGui::InputText("##google_key", state_.translation_config().google_api_key.data(), state_.translation_config().google_api_key.size(), ImGuiInputTextFlags_Password);
            ImGui::TextDisabled("%s", i18n::get("dialog.settings.google_note"));
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
        {
            ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
            ImGui::SetNextItemWidth(300.0f);
            zhipu_key_changed = ImGui::InputText("##zhipu_key", state_.translation_config().zhipu_api_key.data(), state_.translation_config().zhipu_api_key.size(), ImGuiInputTextFlags_Password);
        }

        // Check if any config field changed
        bool any_field_changed = enable_changed || auto_apply_changed || backend_changed || lang_changed || 
                                 base_url_changed || model_changed || openai_key_changed || google_key_changed || zhipu_key_changed || qwen_key_changed || niutrans_key_changed;
        
        // Auto-clear test result when config changes
        if (any_field_changed && !test_result_.empty())
        {
            test_result_.clear();
            test_timestamp_.clear();
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::QwenMT)
        {
            ImGui::TextUnformatted(i18n::get("dialog.settings.model"));
            ImGui::SetNextItemWidth(300.0f);
            int qidx = 1; // default turbo
            if (std::string(state_.translation_config().qwen_model.data()).find("qwen-mt-plus") == 0) qidx = 0;
            const char* qwen_models[] = { "qwen-mt-plus", "qwen-mt-turbo" };
            if (ImGui::Combo("##qwen_model", &qidx, qwen_models, IM_ARRAYSIZE(qwen_models)))
            {
                const char* sel = qwen_models[qidx];
                std::snprintf(state_.translation_config().qwen_model.data(), state_.translation_config().qwen_model.size(), "%s", sel);
                model_changed = true;
            }

            ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
            ImGui::SetNextItemWidth(300.0f);
            qwen_key_changed = ImGui::InputText("##qwen_key", state_.translation_config().qwen_api_key.data(), state_.translation_config().qwen_api_key.size(), ImGuiInputTextFlags_Password);
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Niutrans)
        {
            ImGui::TextUnformatted(i18n::get("dialog.settings.api_key"));
            ImGui::SetNextItemWidth(300.0f);
            niutrans_key_changed = ImGui::InputText("##niutrans_key", state_.translation_config().niutrans_api_key.data(), state_.translation_config().niutrans_api_key.size(), ImGuiInputTextFlags_Password);
        }
        
        // Auto-apply changes if enabled
        if (state_.translation_config().auto_apply_changes && any_field_changed)
        {
            initTranslatorIfEnabled();
            apply_hint_ = i18n::get("dialog.settings.apply_hint");
            apply_hint_timer_ = 5.0f;
        }
        
        ImGui::Spacing();
        
        // Manual Apply button (only shown if auto-apply is off)
        if (!state_.translation_config().auto_apply_changes)
        {
            if (ImGui::Button(i18n::get("apply")))
            {
                initTranslatorIfEnabled();
                apply_hint_ = i18n::get("dialog.settings.apply_hint");
                apply_hint_timer_ = 5.0f;
            }
            ImGui::SameLine();
        }
        
        // Test button
        if (ImGui::Button(i18n::get("dialog.settings.test")) && !testing_connection_)
        {
            testing_connection_ = true;
            test_result_ = i18n::get("dialog.settings.testing");
            
            // Create temporary translator for testing
            translate::TranslatorConfig test_cfg;
            test_cfg.backend = static_cast<translate::Backend>(state_.translation_config().translation_backend);
            switch (state_.translation_config().target_lang_enum)
            {
            case TranslationConfig::TargetLang::EN_US: test_cfg.target_lang = "en-us"; break;
            case TranslationConfig::TargetLang::ZH_CN: test_cfg.target_lang = "zh-cn"; break;
            case TranslationConfig::TargetLang::ZH_TW: test_cfg.target_lang = "zh-tw"; break;
            }
            if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
            {
                test_cfg.base_url = state_.translation_config().openai_base_url.data();
                test_cfg.model = state_.translation_config().openai_model.data();
                test_cfg.api_key = state_.translation_config().openai_api_key.data();
            }
            else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
            {
                test_cfg.base_url.clear();
                test_cfg.model.clear();
                test_cfg.api_key = state_.translation_config().google_api_key.data();
            }
            else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::ZhipuGLM)
            {
                test_cfg.base_url = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
                test_cfg.model = "glm-4-flash";
                test_cfg.api_key = state_.translation_config().zhipu_api_key.data();
            }
            else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Niutrans)
            {
                // Use default endpoint; only API key is required
                test_cfg.base_url = "https://api.niutrans.com/NiuTransServer/translation";
                test_cfg.model.clear();
                test_cfg.api_key = state_.translation_config().niutrans_api_key.data();
            }
            
            auto temp_translator = translate::createTranslator(test_cfg.backend);
            if (temp_translator && temp_translator->init(test_cfg))
            {
                test_result_ = temp_translator->testConnection();
            }
            else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::QwenMT)
            {
                test_cfg.base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
                const char* qm = state_.translation_config().qwen_model.data();
                test_cfg.model = (qm && qm[0]) ? qm : "qwen-mt-turbo";
                test_cfg.api_key = state_.translation_config().qwen_api_key.data();
            }
            else
            {
                test_result_ = "Error: Failed to initialize translator for testing";
            }
            if (temp_translator) temp_translator->shutdown();
            
            // Capture timestamp
            std::time_t now = std::time(nullptr);
            std::tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &now);
#else
            localtime_r(&now, &tm_buf);
#endif
            char time_str[16];
            std::strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
            test_timestamp_ = time_str;
            
            testing_connection_ = false;
        }
        
        // Status indicator
        const char* status = (translator_ && translator_->isReady()) ? i18n::get("dialog.settings.ready") : i18n::get("dialog.settings.not_ready");
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s", i18n::get("dialog.settings.status_label"), status);

        // Apply success hint (auto-clears after 5 seconds)
        if (apply_hint_timer_ > 0.0f)
        {
            apply_hint_timer_ -= io.DeltaTime;
            if (apply_hint_timer_ <= 0.0f)
            {
                apply_hint_.clear();
                apply_hint_timer_ = 0.0f;
            }
        }
        if (!apply_hint_.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "%s", apply_hint_.c_str());
        }
        
        if (translator_)
        {
            const char* err = translator_->lastError();
            if (err && err[0]) ImGui::TextColored(UITheme::warningColor(), "%s", err);
        }
        
        // Show test results if available
        if (!test_result_.empty())
        {
            ImVec4 color;
            if (test_result_.find("Success:") == 0)
                color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);  // Green for success
            else if (test_result_.find("Warning:") == 0)
                color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow for warning
            else if (test_result_.find("Error:") == 0 || test_result_.find("Testing") == 0)
                color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);  // Red for error/testing
            else
                color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Grey for other
            
            // Display with inline timestamp if available
            if (!test_timestamp_.empty())
            {
                std::string line = i18n::format("dialog.settings.test_result", {{"time", test_timestamp_}, {"text", test_result_}});
                ImGui::TextColored(color, "%s", line.c_str());
            }
            else
            {
                std::string line = i18n::format("dialog.settings.test_result_no_time", {{"text", test_result_}});
                ImGui::TextColored(color, "%s", line.c_str());
            }
        }
        
        ImGui::Unindent();
        ImGui::Spacing();
    }

    // DEBUG SECTION
    if (ImGui::CollapsingHeader(i18n::get("dialog.debug.title")))
    {
        ImGui::Indent();
        ImGui::PushID(settings_id_suffix_.c_str());
        
        // IPC Section
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Font Section
        ImGui::TextUnformatted(i18n::get("dialog.settings.font_path"));
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float avail = ImGui::GetContentRegionAvail().x;
            float btn_w = ImGui::CalcTextSize(i18n::get("dialog.settings.reload_font")).x + style.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(220.0f, avail - btn_w - style.ItemSpacing.x));
            ImGui::InputText("##font_path", state_.ui_state().font_path.data(), state_.ui_state().font_path.size());
            ImGui::SameLine();
            if (ImGui::Button(i18n::get("dialog.settings.reload_font")))
            {
                bool loaded = font_manager_.reloadFont(state_.ui_state().font_path.data());
                state_.ui_state().has_custom_font = loaded;
            }
            ImGui::TextDisabled("%s %s", i18n::get("dialog.settings.font_active_label"), state_.ui_state().has_custom_font ? i18n::get("dialog.settings.font_active_custom") : i18n::get("dialog.settings.font_active_default"));
            if (!state_.ui_state().has_custom_font)
                ImGui::TextColored(UITheme::warningColor(), "%s", i18n::get("dialog.settings.font_warning_no_cjk"));
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Cache Stats Section
        ImGui::TextUnformatted(i18n::get("dialog.settings.translation_cache"));
        {
            std::string t = i18n::format("dialog.settings.cache_entries", {{"cur", std::to_string(session_.cacheEntries())}, {"cap", std::to_string(session_.cacheCapacity())}});
            ImGui::TextUnformatted(t.c_str());
        }
        {
            std::string t = i18n::format("dialog.settings.cache_hits", {{"n", std::to_string(static_cast<unsigned long long>(session_.cacheHits()))}});
            ImGui::TextUnformatted(t.c_str());
        }
        {
            std::string t = i18n::format("dialog.settings.cache_misses", {{"n", std::to_string(static_cast<unsigned long long>(session_.cacheMisses()))}});
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
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Dialog Texts Section
        ImGui::TextUnformatted(i18n::get("dialog.settings.appended_texts"));
        // Wrap list in a child region to ensure proper clipping
        if (ImGui::BeginChild("SegmentsChild", ImVec2(0, 220.0f), ImGuiChildFlags_Border))
        {
            int to_delete = -1;
            for (int i = 0; i < static_cast<int>(state_.content_state().segments.size()); ++i)
            {
                ImGui::PushID(i);
                const ImGuiStyle& style = ImGui::GetStyle();
                float row_avail = ImGui::GetContentRegionAvail().x;
                // Reserve space for Edit and Delete buttons
                float edit_w = ImGui::CalcTextSize(i18n::get("dialog.append.edit")).x + style.FramePadding.x * 2.0f;
                float del_w  = ImGui::CalcTextSize(i18n::get("dialog.append.delete")).x + style.FramePadding.x * 2.0f;
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

                    const char* full = state_.content_state().segments[i].data();
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
                if (ImGui::SmallButton(i18n::get("dialog.append.edit")))
                {
                    state_.content_state().editing_index = i;
                    std::snprintf(state_.content_state().edit_buffer.data(), state_.content_state().edit_buffer.size(), "%s", state_.content_state().segments[i].data());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(i18n::get("dialog.append.delete")))
                    to_delete = i;
                ImGui::PopID();
            }
            if (to_delete >= 0 && to_delete < static_cast<int>(state_.content_state().segments.size()))
                state_.content_state().segments.erase(state_.content_state().segments.begin() + to_delete);
        }
        ImGui::EndChild();

        // Full editor for selected entry
        if (state_.content_state().editing_index >= 0 && state_.content_state().editing_index < static_cast<int>(state_.content_state().segments.size()))
        {
            ImGui::Spacing();
            {
                std::string t = i18n::format("dialog.append.editing_entry", {{"index", std::to_string(state_.content_state().editing_index)}});
                ImGui::TextDisabled("%s", t.c_str());
            }
            ImVec2 box(0, 160.0f);
            ImGui::InputTextMultiline("##full_editor", state_.content_state().edit_buffer.data(), state_.content_state().edit_buffer.size(), box);
            if (ImGui::Button(i18n::get("common.save")))
            {
                // Save back to segment (truncate to buffer size)
                safe_copy_utf8(state_.content_state().segments[state_.content_state().editing_index].data(), state_.content_state().segments[state_.content_state().editing_index].size(), state_.content_state().edit_buffer.data());
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

        ImGui::Spacing();
        ImGui::TextUnformatted(i18n::get("dialog.append.new_text"));
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float append_avail = ImGui::GetContentRegionAvail().x;
            float btn_w = ImGui::CalcTextSize(i18n::get("dialog.append.append_button")).x + style.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(220.0f, append_avail - btn_w - style.ItemSpacing.x));
            ImGui::InputText("##append", state_.content_state().append_buffer.data(), state_.content_state().append_buffer.size());
            ImGui::SameLine();
            if (ImGui::Button(i18n::get("dialog.append.append_button")))
            {
                if (state_.content_state().append_buffer[0] != '\0')
                {
                    state_.content_state().segments.emplace_back();
                    safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), state_.content_state().append_buffer.data());
                    state_.content_state().append_buffer[0] = '\0';
                }
            }
        }
        
        ImGui::PopID();
        ImGui::Unindent();
        ImGui::Spacing();
    }

    if (width_changed)
    {
        state_.ui_state().window_size.x   = state_.ui_state().width;
        state_.ui_state().pending_resize  = true;
    }
    if (height_changed)
    {
        state_.ui_state().window_size.y   = state_.ui_state().height;
        state_.ui_state().pending_resize  = true;
    }
    if (alpha_changed || font_changed)
        state_.ui_state().pending_resize = state_.ui_state().pending_resize;
}

// Handle right-click context menu for dialog window
void DialogWindow::renderDialogContextMenu()
{
    // Check if mouse is within this dialog window bounds
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    bool within_dialog = ImGui::IsMousePosValid(&mouse_pos) &&
        ImGui::IsMouseHoveringRect(state_.ui_state().window_pos,
            ImVec2(state_.ui_state().window_pos.x + state_.ui_state().window_size.x,
                   state_.ui_state().window_pos.y + state_.ui_state().window_size.y), false);

    // Open context menu on right-click within dialog bounds
    if (within_dialog && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(("DialogContextMenu###" + id_suffix_).c_str());
    }

    // Render the context menu
    std::string popup_id = "DialogContextMenu###" + id_suffix_;
    if (ImGui::BeginPopup(popup_id.c_str()))
    {
        if (ImGui::MenuItem(i18n::get("common.settings")))
        {
            show_settings_window_ = !show_settings_window_;
        }
        
        if (ImGui::MenuItem(i18n::get("common.remove")))
        {
            // Signal for removal - we'll handle this in the registry
            should_be_removed_ = true;
        }
        
        ImGui::EndPopup();
    }
}

void DialogWindow::renderSettingsWindow(ImGuiIO& io)
{
    if (!show_settings_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(480.0f, 560.0f), ImGuiCond_FirstUseEver);
    std::string settings_title = name_ + " " + std::string(i18n::get("dialog.settings.window_suffix")) + "###" + settings_id_suffix_;
    if (ImGui::Begin(settings_title.c_str(), &show_settings_window_))
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
