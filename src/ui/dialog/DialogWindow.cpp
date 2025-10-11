#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>
#include "../DockState.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "../../translate/ITranslator.hpp"
#include "../../processing/TextPipeline.hpp"
#include "../../processing/TextNormalizer.hpp"
#include "../../config/ConfigManager.hpp"
#include "../UITheme.hpp"
#include "../../services/DQXClarityService.hpp"
#include "../../services/DQXClarityLauncher.hpp"
#include "../../dqxclarity/api/dialog_message.hpp"
#include "../Localization.hpp"
#include "DialogAnimator.hpp"
#include "../../dqxclarity/api/dqxclarity.hpp"

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

    std::string strip_waiting_suffix(std::string text)
    {
        auto ends_with = [](const std::string& value, const std::string& suffix) {
            return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        const std::string ellipsis = "\xE2\x80\xA6";        // …
        const std::string full_stop = "\xE3\x80\x82";       // 。
        const std::string fullwidth_period = "\xEF\xBC\x8E"; // ．

        while (!text.empty())
        {
            bool stripped = false;
            if (!text.empty() && text.back() == '.')
            {
                text.pop_back();
                stripped = true;
            }
            else if (ends_with(text, ellipsis))
            {
                text.erase(text.size() - ellipsis.size());
                stripped = true;
            }
            else if (ends_with(text, full_stop))
            {
                text.erase(text.size() - full_stop.size());
                stripped = true;
            }
            else if (ends_with(text, fullwidth_period))
            {
                text.erase(text.size() - fullwidth_period.size());
                stripped = true;
            }

            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            {
                text.pop_back();
                stripped = true;
            }

            if (!stripped)
                break;
        }
        return text;
    }

    std::string localized_or_fallback(const char* key, const char* fallback)
    {
        const std::string& value = i18n::get_str(key);
        if (value.empty() || value == key)
            return std::string(fallback);
        return value;
    }
    
}


void DialogWindow::ensurePlaceholderEntry()
{
    if (state_.content_state().segments.empty()) {
        state_.content_state().segments.emplace_back();
        state_.content_state().segments.back().fill('\0');
    }
    if (state_.content_state().speakers.size() < state_.content_state().segments.size()) {
        state_.content_state().speakers.resize(state_.content_state().segments.size());
    }
}

void DialogWindow::setPlaceholderText(const std::string& text, PlaceholderState state)
{
    ensurePlaceholderEntry();
    auto& buf = state_.content_state().segments[0];
    buf.fill('\0');
    safe_copy_utf8(buf.data(), buf.size(), text);
    if (!state_.content_state().speakers.empty()) {
        state_.content_state().speakers[0].clear();
    }
    placeholder_active_ = true;
    placeholder_state_ = state;
    placeholder_base_text_ = text;
    if (state == PlaceholderState::Waiting) {
        wait_anim_.reset();
    }
}

void DialogWindow::reinitializePlaceholder()
{
    resetPlaceholder();
}

void DialogWindow::resetPlaceholder()
{
    setPlaceholderText(i18n::get("dialog.placeholder.waiting"), PlaceholderState::Waiting);
}

void DialogWindow::refreshPlaceholderStatus()
{
    if (!placeholder_active_) {
        return;
    }

    const std::string waiting = localized_or_fallback("dialog.placeholder.waiting", "Initializing dialog system...");
    const std::string ready = localized_or_fallback("dialog.placeholder.ready", "Dialog system ready.");
    const std::string failed = localized_or_fallback("dialog.placeholder.failed", "Dialog system failed to initialize. Check hook status and logs.");

    ensurePlaceholderEntry();

    auto set_if_changed = [&](const std::string& text, PlaceholderState state) {
        if (placeholder_state_ != state || placeholder_base_text_ != text) {
            setPlaceholderText(text, state);
        }
    };

    auto* launcher = DQXClarityService_Get();
    if (!launcher) {
        set_if_changed(waiting, PlaceholderState::Waiting);
        return;
    }

    auto stage = launcher->getEngineStage();
    if (stage == dqxclarity::Status::Hooked) {
        set_if_changed(ready, PlaceholderState::Ready);
    } else if (stage == dqxclarity::Status::Error) {
        set_if_changed(failed, PlaceholderState::Error);
    } else {
        set_if_changed(waiting, PlaceholderState::Waiting);
    }
}

int DialogWindow::appendSegmentInternal(const std::string& speaker, const std::string& text)
{
    std::string collapsed_text = processing::collapse_newlines(text);
    if (placeholder_active_) {
        ensurePlaceholderEntry();
        safe_copy_utf8(state_.content_state().segments[0].data(), state_.content_state().segments[0].size(), collapsed_text);
        if (!state_.content_state().speakers.empty()) {
            state_.content_state().speakers[0] = speaker;
        }
        placeholder_active_ = false;
        placeholder_state_ = PlaceholderState::Ready;
        placeholder_base_text_.clear();
        return 0;
    }

    state_.content_state().segments.emplace_back();
    state_.content_state().segments.back().fill('\0');
    safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), collapsed_text);
    state_.content_state().speakers.push_back(speaker);
    return static_cast<int>(state_.content_state().segments.size()) - 1;
}


DialogWindow::DialogWindow(FontManager& font_manager, int instance_id, const std::string& name)
    : font_manager_(font_manager)
    , settings_view_(state_, font_manager_, session_)
    , cached_backend_(translate::Backend::OpenAI)
{

    name_ = name;
    id_suffix_ = "dialog_window_" + std::to_string(instance_id);
    settings_id_suffix_ = "dialog_settings_" + std::to_string(instance_id);
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
    
    text_pipeline_ = std::make_unique<processing::TextPipeline>();

    state_.applyDefaults();

    resetPlaceholder();

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
            for (auto& m : msgs)
            {
                bool hasValidText = !m.text.empty() && !is_blank(m.text);
                bool hasValidSpeaker = !m.speaker.empty() && m.speaker != "No_NPC";
                if (hasValidText || hasValidSpeaker)
                {
                    PendingMsg pm; pm.text = std::move(m.text); pm.lang = std::move(m.lang); pm.speaker = std::move(m.speaker); pm.seq = m.seq;
                    pending_.push(std::move(pm));
                }
            }
        }
    }

    std::vector<PendingMsg> local;
    {
        pending_.drain(local);
    }
    if (local.empty())
        return;

    if (state_.translation_config().translate_enabled) {
        if (!translator_ || !translator_->isReady()) {
            initTranslatorIfEnabled();
        }
    }

    appended_since_last_frame_ = true;
    for (auto& m : local)
    {
        if (state_.translation_config().translate_enabled)
        {
            // Handle empty text for NPC-only messages
            std::string text_to_process = m.text.empty() ? " " : m.text;
            
            std::string processed_text = text_pipeline_->process(text_to_process);

            auto backend_before = state_.translation_config().translation_backend;
            auto submit = session_.submit(
                processed_text,
                backend_before,
                state_.translation_config().target_lang_enum,
                translator_.get());

            if (submit.kind == TranslateSession::SubmitKind::Queued) {
                PLOG_INFO << "Queued translation job " << submit.job_id;
            } else if (submit.kind == TranslateSession::SubmitKind::DroppedNotReady) {
                PLOG_WARNING << "Dropped translation request (translator not ready); backend="
                             << static_cast<int>(backend_before);
            } else if (submit.kind == TranslateSession::SubmitKind::Cached) {
                PLOG_INFO << "Served translation from cache";
            }

            if (submit.kind == TranslateSession::SubmitKind::Cached)
            {
                appendSegmentInternal(m.speaker, submit.text);
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
                std::string placeholder = waiting_text_for_lang(state_.translation_config().target_lang_enum);
                placeholder += wait_anim_.suffix();
                int idx = appendSegmentInternal(m.speaker, placeholder);
                if (job_id != 0)
                {
                    pending_segment_by_job_[job_id] = idx;
                }
            }

            if (m.seq > 0) last_applied_seq_ = m.seq;
        }
        else
        {
            std::string text_to_copy = m.text.empty() ? " " : m.text;
            appendSegmentInternal(m.speaker, text_to_copy);
        }
        if (m.seq > 0)
        {
            last_applied_seq_ = m.seq;
            // No ack needed in in-process mode
        }
    }
}


void DialogWindow::render()
{
    appended_since_last_frame_ = false;
    refreshPlaceholderStatus();
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
                        if (ev.failed)
                        {
                            std::string failure_msg = i18n::get_str("dialog.translate.timeout.failed") + " " + ev.original_text;
                            safe_copy_utf8(state_.content_state().segments[idx].data(), state_.content_state().segments[idx].size(), failure_msg);
                            failed_segments_.insert(idx);
                            failed_original_text_[idx] = ev.original_text;
                            failed_error_messages_[idx] = ev.error_message;
                        }
                        else
                        {
                            std::string collapsed_text = processing::collapse_newlines(ev.text);
                            safe_copy_utf8(state_.content_state().segments[idx].data(), state_.content_state().segments[idx].size(), collapsed_text);
                            failed_segments_.erase(idx);
                            failed_original_text_.erase(idx);
                            failed_error_messages_.erase(idx);
                        }
                    }
                    pending_segment_by_job_.erase(it);
                }
                else
                {
                    state_.content_state().segments.emplace_back();
                    state_.content_state().speakers.emplace_back();
                    int new_idx = static_cast<int>(state_.content_state().segments.size()) - 1;
                    if (ev.failed)
                    {
                        std::string failure_msg = i18n::get_str("dialog.translate.timeout.failed") + " " + ev.original_text;
                        safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), failure_msg);
                        failed_segments_.insert(new_idx);
                        failed_original_text_[new_idx] = ev.original_text;
                        failed_error_messages_[new_idx] = ev.error_message;
                    }
                    else
                    {
                        std::string collapsed_text = processing::collapse_newlines(ev.text);
                        safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), collapsed_text);
                    }
                }
            }
        }
    }

    renderDialog();
    renderDialogContextMenu();
    renderSettingsWindow();
}

// Renders the per-instance settings UI.
void DialogWindow::renderSettings()
{
    // If config manager recently reported a parse error from manual edits, surface it here
    if (auto* cm = ConfigManager_Get())
    {
        if (const char* err = cm->lastError(); err && err[0])
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err);
        }
    }
    renderSettingsPanel();
}

void DialogWindow::renderDialog()
{
    ImGuiIO& io = ImGui::GetIO();
    const float max_dialog_width  = std::max(200.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(120.0f, io.DisplaySize.y - 40.0f);

    state_.ui_state().width  = std::clamp(state_.ui_state().width, 200.0f, max_dialog_width);
    state_.ui_state().height = std::clamp(state_.ui_state().height, 80.0f, max_dialog_height);
    state_.ui_state().padding.x        = std::clamp(state_.ui_state().padding.x, 4.0f, 80.0f);
    state_.ui_state().padding.y        = std::clamp(state_.ui_state().padding.y, 4.0f, 80.0f);
    state_.ui_state().rounding         = std::clamp(state_.ui_state().rounding, 0.0f, 32.0f);
    state_.ui_state().border_thickness = std::clamp(state_.ui_state().border_thickness, 0.5f, 6.0f);
    
    // Auto-fade logic: update timer and calculate alpha multiplier (per-dialog setting)
    bool fade_enabled = state_.ui_state().fade_enabled;
    float fade_timeout = state_.ui_state().fade_timeout;
    
    if (fade_enabled)
    {
        // Initialize timer on first frame
        if (state_.ui_state().last_activity_time == 0.0f)
        {
            state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
        }
        
        // Reset timer on new text append
        if (appended_since_last_frame_)
        {
            state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
            state_.ui_state().current_alpha_multiplier = 1.0f;
        }
        
        // Calculate time since last activity
        float current_time = static_cast<float>(ImGui::GetTime());
        float time_since_activity = current_time - state_.ui_state().last_activity_time;
        
        // Calculate fade effect
        // Start fading at 75% of timeout (15s for 20s timeout)
        // Complete fade at 100% of timeout (20s)
        float fade_start = fade_timeout * 0.75f;
        float fade_duration = fade_timeout * 0.25f;
        
        if (time_since_activity >= fade_start)
        {
            float fade_progress = (time_since_activity - fade_start) / fade_duration;
            fade_progress = std::clamp(fade_progress, 0.0f, 1.0f);
            
            // Smooth fade curve (ease-in)
            state_.ui_state().current_alpha_multiplier = 1.0f - (fade_progress * fade_progress);
        }
        else
        {
            state_.ui_state().current_alpha_multiplier = 1.0f;
        }
    }
    else
    {
        state_.ui_state().current_alpha_multiplier = 1.0f;
    }

    if (auto* cm = ConfigManager_Get())
    {
        if (DockState::IsScattering())
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
        }
        else if (cm->getAppMode() == ConfigManager::AppMode::Mini)
        {
            // Lock dialog into the dockspace while in Mini mode
            ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
        }
    }

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

    // Apply fade multiplier to background alpha and border
    float effective_alpha = state_.ui_state().background_alpha * state_.ui_state().current_alpha_multiplier;
    UITheme::pushDialogStyle(effective_alpha, state_.ui_state().padding, state_.ui_state().rounding, state_.ui_state().border_thickness, state_.ui_state().border_enabled, state_.ui_state().current_alpha_multiplier);

    ImGuiWindowFlags dialog_flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoCollapse;

    if (auto* cm = ConfigManager_Get())
    {
        if (cm->getAppMode() == ConfigManager::AppMode::Mini)
        {
            dialog_flags |= ImGuiWindowFlags_NoMove;
        }
    }

    if (ImGui::Begin(window_label_.c_str(), nullptr, dialog_flags))
    {
        // Check if mouse is hovering over the dialog window
        bool is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (fade_enabled && is_hovered)
        {
            // Reset fade timer on hover
            state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
            state_.ui_state().current_alpha_multiplier = 1.0f;
        }
        
        renderVignette(
            ImGui::GetWindowPos(),
            ImGui::GetWindowSize(),
            state_.ui_state().vignette_thickness,
            state_.ui_state().rounding,
            state_.ui_state().current_alpha_multiplier
        );

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
        
        // Check if window is docked (must be called inside Begin/End)
        bool is_docked = ImGui::IsWindowDocked();
        state_.ui_state().is_docked = is_docked;

        bool animate_placeholder = placeholder_active_ && placeholder_state_ == PlaceholderState::Waiting;
        bool animate_translations = !pending_segment_by_job_.empty();

        if (animate_placeholder || animate_translations)
        {
            wait_anim_.advance(io.DeltaTime);
        }

        if (animate_placeholder)
        {
            ensurePlaceholderEntry();
            std::string base = placeholder_base_text_;
            if (base.empty()) {
                base = localized_or_fallback("dialog.placeholder.waiting", "Initializing dialog system...");
            }
            std::string trimmed = strip_waiting_suffix(base);
            const char* dots = wait_anim_.suffix();
            std::string composed;
            if (trimmed.empty())
                composed = dots;
            else
                composed = trimmed + dots;
            safe_copy_utf8(state_.content_state().segments[0].data(), state_.content_state().segments[0].size(), composed);
        }

        // Update 'Waiting...' placeholder animation for in-flight translations
        if (animate_translations)
        {
            std::string base = waiting_text_for_lang(state_.translation_config().target_lang_enum);
            const char* dots = wait_anim_.suffix();
            for (const auto& kv : pending_segment_by_job_)
            {
                int idx = kv.second;
                if (idx >= 0 && idx < static_cast<int>(state_.content_state().segments.size()))
                {
                    std::string composed = base;
                    composed += dots;
                    safe_copy_utf8(state_.content_state().segments[idx].data(), state_.content_state().segments[idx].size(), composed);
                }
            }
        }

        // Filter out No_NPC and names containing corruption characters
        auto isValidNpcName = [](const std::string& name) -> bool {
            if (name.empty() || name == "No_NPC") return false;
            
            // Filter names containing corruption characters
            if (name.find('?') != std::string::npos ||
                name.find('(') != std::string::npos ||
                name.find(')') != std::string::npos ||
                name.find('<') != std::string::npos ||
                name.find('_') != std::string::npos ||
                name.find('^') != std::string::npos ||
                name.find('>') != std::string::npos) {
                return false;
            }
            
            return true;
        };
        
        
        for (size_t i = 0; i < state_.content_state().segments.size(); ++i)
        {
            // Determine if we have a valid NPC name for this segment
            bool hasValidNpc = false;
            std::string currentSpeaker;
            if (i < state_.content_state().speakers.size()) {
                currentSpeaker = state_.content_state().speakers[i];
                hasValidNpc = isValidNpcName(currentSpeaker);
                
            }
            
            ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
            ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
            float content_width = cr_max.x - cr_min.x;
            
            renderSeparator(hasValidNpc, currentSpeaker, content_width, state_.ui_state().current_alpha_multiplier);
            
            ImVec2 pos = ImGui::GetCursorScreenPos();
            const char* txt = state_.content_state().segments[i].data();

            bool placeholder_failed = (placeholder_active_ && placeholder_state_ == PlaceholderState::Error && i == 0);
            if (placeholder_failed)
            {
                ImVec4 err_color(1.0f, 0.4f, 0.3f, 1.0f);
                err_color.w *= state_.ui_state().current_alpha_multiplier;
                ImGui::PushStyleColor(ImGuiCol_Text, err_color);
            }

            renderOutlinedText(
                txt,
                pos,
                ImGui::GetFont(),
                ImGui::GetFontSize(),
                wrap_width,
                state_.ui_state().current_alpha_multiplier
            );

            if (placeholder_failed)
            {
                ImGui::PopStyleColor();
            }

            ImVec2 text_sz = ImGui::CalcTextSize(txt, nullptr, false, wrap_width);
            ImGui::Dummy(ImVec2(0.0f, text_sz.y));
            
            // Show copy/retry buttons for failed translations
            if (failed_segments_.count(static_cast<int>(i)))
            {
                ImGui::Spacing();
                
                // Display error message if available (with fade)
                auto err_it = failed_error_messages_.find(static_cast<int>(i));
                if (err_it != failed_error_messages_.end() && !err_it->second.empty())
                {
                    std::string reason_label = i18n::get_str("dialog.translate.timeout.reason");
                    ImVec4 reason_color(1.0f, 0.6f, 0.4f, 1.0f);
                    reason_color.w *= state_.ui_state().current_alpha_multiplier;
                    ImGui::TextColored(reason_color, "%s %s", reason_label.c_str(), err_it->second.c_str());
                    ImGui::Spacing();
                }
                
                std::string copy_btn_id = std::string(i18n::get("dialog.translate.timeout.copy")) + "##copy_" + std::to_string(i);
                std::string retry_btn_id = std::string(i18n::get("dialog.translate.timeout.retry")) + "##retry_" + std::to_string(i);
                
                if (ImGui::Button(copy_btn_id.c_str()))
                {
                    auto it = failed_original_text_.find(static_cast<int>(i));
                    if (it != failed_original_text_.end())
                    {
                        ImGui::SetClipboardText(it->second.c_str());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(retry_btn_id.c_str()))
                {
                    auto it = failed_original_text_.find(static_cast<int>(i));
                    if (it != failed_original_text_.end() && translator_ && translator_->isReady())
                    {
                        std::string text_to_retry = it->second;
                        std::string processed_text = text_pipeline_->process(text_to_retry);
                        auto submit = session_.submit(
                            processed_text,
                            state_.translation_config().translation_backend,
                            state_.translation_config().target_lang_enum,
                            translator_.get());
                        
                        if (submit.kind == TranslateSession::SubmitKind::Queued && submit.job_id != 0)
                        {
                            pending_segment_by_job_[submit.job_id] = static_cast<int>(i);
                            std::string placeholder = std::string(waiting_text_for_lang(state_.translation_config().target_lang_enum)) + " .";
                            safe_copy_utf8(state_.content_state().segments[i].data(), state_.content_state().segments[i].size(), placeholder);
                            failed_segments_.erase(static_cast<int>(i));
                            failed_original_text_.erase(static_cast<int>(i));
                            failed_error_messages_.erase(static_cast<int>(i));
                        }
                    }
                }
                ImGui::Spacing();
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
        translator_initialized_ = false;
        cached_translator_config_ = translate::TranslatorConfig{};
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

    bool same_backend = translator_initialized_ && translator_ && cfg.backend == cached_backend_;
    bool same_config = same_backend;
    if (same_config) {
        same_config = (cfg.base_url == cached_translator_config_.base_url &&
                       cfg.model == cached_translator_config_.model &&
                       cfg.api_key == cached_translator_config_.api_key &&
                       cfg.target_lang == cached_translator_config_.target_lang);
    }
    if (same_config && translator_->isReady()) {
        return;
    }

    if (translator_) {
        translator_->shutdown();
        translator_.reset();
    }
    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg))
    {
        if (translator_) {
            PLOG_WARNING << "Translator init failed for backend " << static_cast<int>(cfg.backend);
        } else {
            PLOG_WARNING << "Translator factory returned null for backend " << static_cast<int>(cfg.backend);
        }
        translator_.reset();
        translator_initialized_ = false;
        return;
    }

    if (!translator_->isReady())
    {
        PLOG_WARNING << "Translator not ready after init for backend " << static_cast<int>(cfg.backend);
        translator_initialized_ = false;
    }
    else
    {
        PLOG_INFO << "Translator ready for backend " << static_cast<int>(cfg.backend);
        cached_translator_config_ = cfg;
        cached_backend_ = static_cast<translate::Backend>(state_.translation_config().translation_backend);
        translator_initialized_ = true;
    }
}


void DialogWindow::renderSettingsPanel()
{
    settings_view_.render(
        translator_.get(),
        apply_hint_,
        apply_hint_timer_,
        testing_connection_,
        test_result_,
        test_timestamp_,
        settings_id_suffix_,
        [this]() { this->initTranslatorIfEnabled(); },
        [this]() -> translate::ITranslator* { return translator_.get(); }
    );
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

    // Use cached docked state from render
    bool is_docked = state_.ui_state().is_docked;
    
    // Get total dialog count from config manager registry
    int dialog_count = 0;
    if (auto* cm = ConfigManager_Get())
    {
        if (auto* reg = cm->registry())
        {
            dialog_count = static_cast<int>(reg->windowsByType(UIWindowType::Dialog).size());
        }
    }

    // Render the context menu
    std::string popup_id = "DialogContextMenu###" + id_suffix_;
    if (ImGui::BeginPopup(popup_id.c_str()))
    {
        if (ImGui::MenuItem(i18n::get("common.settings")))
        {
            show_settings_window_ = !show_settings_window_;
        }
        
        ImGui::Separator();
        
        // Font size controls
        float min_font = std::max(8.0f, state_.ui_state().font_base_size * 0.5f);
        float max_font = state_.ui_state().font_base_size * 2.5f;
        bool can_increase = state_.ui_state().font_size < max_font;
        bool can_decrease = state_.ui_state().font_size > min_font;
        
        if (ImGui::MenuItem(i18n::get("dialog.context_menu.increase_font"), nullptr, false, can_increase))
        {
            state_.ui_state().font_size = std::min(state_.ui_state().font_size + 2.0f, max_font);
        }
        
        if (ImGui::MenuItem(i18n::get("dialog.context_menu.decrease_font"), nullptr, false, can_decrease))
        {
            state_.ui_state().font_size = std::max(state_.ui_state().font_size - 2.0f, min_font);
        }
        
        ImGui::Separator();
        
        // Disable remove button if this is the only dialog
        bool can_remove = dialog_count > 1;
        if (ImGui::MenuItem(i18n::get("common.remove"), nullptr, false, can_remove))
        {
            // Signal for removal - we'll handle this in the registry
            should_be_removed_ = true;
        }
        
        // Add global options when docked
        if (is_docked)
        {
            ImGui::Separator();
            
            if (ImGui::MenuItem(i18n::get("menu.global_settings")))
            {
                if (auto* cm = ConfigManager_Get())
                {
                    cm->requestShowGlobalSettings();
                }
            }
            
            if (ImGui::BeginMenu(i18n::get("menu.app_mode")))
            {
                if (auto* cm = ConfigManager_Get())
                {
                    auto mode = cm->getAppMode();
                    bool sel_normal = (mode == ConfigManager::AppMode::Normal);
                    bool sel_borderless = (mode == ConfigManager::AppMode::Borderless);
                    bool sel_mini = (mode == ConfigManager::AppMode::Mini);
                    if (ImGui::MenuItem(i18n::get("settings.app_mode.items.normal"), nullptr, sel_normal)) cm->setAppMode(ConfigManager::AppMode::Normal);
                    if (ImGui::MenuItem(i18n::get("settings.app_mode.items.borderless"), nullptr, sel_borderless)) cm->setAppMode(ConfigManager::AppMode::Borderless);
                    if (ImGui::MenuItem(i18n::get("settings.app_mode.items.mini"), nullptr, sel_mini)) cm->setAppMode(ConfigManager::AppMode::Mini);
                }
                ImGui::EndMenu();
            }
            
            ImGui::Separator();
            if (ImGui::MenuItem(i18n::get("menu.quit")))
            {
                if (auto* cm = ConfigManager_Get())
                {
                    cm->requestQuit();
                }
            }
        }
        
        ImGui::EndPopup();
    }
}

void DialogWindow::renderSettingsWindow()
{
    if (!show_settings_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(480.0f, 560.0f), ImGuiCond_FirstUseEver);
    std::string settings_title = name_ + " " + std::string(i18n::get("dialog.settings.window_suffix")) + "###" + settings_id_suffix_;
    if (auto* cm = ConfigManager_Get())
    {
        if (DockState::IsScattering())
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
        }
        else if (cm->getAppMode() == ConfigManager::AppMode::Mini)
        {
            ImGuiCond cond = DockState::ShouldReDock() ? ImGuiCond_Always : ImGuiCond_Once;
            ImGui::SetNextWindowDockID(DockState::GetDockspace(), cond);
        }
    }

    if (ImGui::Begin(settings_title.c_str(), &show_settings_window_))
    {
        renderSettingsPanel();
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

void DialogWindow::renderVignette(
    const ImVec2& win_pos,
    const ImVec2& win_size,
    float thickness,
    float rounding,
    float alpha_multiplier
)
{
    thickness = std::max(0.0f, thickness);
    if (thickness <= 0.0f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding0 = std::max(0.0f, rounding);

    int steps = static_cast<int>(std::ceil(thickness * 3.0f));
    steps = std::clamp(steps, 1, 256);

    float max_alpha = std::clamp(0.30f + 0.006f * thickness, 0.30f, 0.65f);

    for (int i = 0; i < steps; ++i)
    {
        float t = (steps <= 1) ? 0.0f : (static_cast<float>(i) / (steps - 1));
        float inset = t * thickness;
        ImVec2 pmin(win_pos.x + inset, win_pos.y + inset);
        ImVec2 pmax(win_pos.x + win_size.x - inset, win_pos.y + win_size.y - inset);
        float r = std::max(0.0f, rounding0 - inset);
        float a = max_alpha * (1.0f - t);
        a = a * a;
        a *= alpha_multiplier;
        if (a <= 0.0f) continue;
        ImU32 col = IM_COL32(0, 0, 0, (int)(a * 255.0f));
        dl->AddRect(pmin, pmax, col, r, 0, 1.0f);
    }
}

void DialogWindow::renderSeparator(
    bool hasNPC,
    const std::string& speaker,
    float content_width,
    float alpha_multiplier
)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
    ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
    float x1 = win_pos.x + cr_min.x;
    float x2 = win_pos.x + cr_max.x;

    ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing()));
    float y = ImGui::GetCursorScreenPos().y;

    if (hasNPC)
    {
        ImVec2 text_size = ImGui::CalcTextSize(speaker.c_str());
        float padding = 10.0f;
        float text_area_width = text_size.x + padding * 2.0f;
        float line_width = (content_width - text_area_width) * 0.5f;

        if (line_width > 5.0f)
        {
            float line_y = y + text_size.y * 0.5f;
            ImVec4 sep_color = UITheme::dialogSeparatorColor();
            sep_color.w *= alpha_multiplier;
            ImU32 sep_col_u32 = ImGui::ColorConvertFloat4ToU32(sep_color);

            draw_list->AddRectFilled(
                ImVec2(x1, line_y),
                ImVec2(x1 + line_width, line_y + UITheme::dialogSeparatorThickness()),
                sep_col_u32
            );
            draw_list->AddRectFilled(
                ImVec2(x2 - line_width, line_y),
                ImVec2(x2, line_y + UITheme::dialogSeparatorThickness()),
                sep_col_u32
            );
        }

        ImVec4 sep_text_color = UITheme::dialogSeparatorColor();
        sep_text_color.w *= alpha_multiplier;
        ImVec2 text_pos((x1 + x2 - text_size.x) * 0.5f, y);
        draw_list->AddText(text_pos, ImGui::ColorConvertFloat4ToU32(sep_text_color), speaker.c_str());

        ImGui::Dummy(ImVec2(0.0f, text_size.y + UITheme::dialogSeparatorSpacing()));
    }
    else
    {
        float line_y = y;
        ImVec4 sep_color = UITheme::dialogSeparatorColor();
        sep_color.w *= alpha_multiplier;
        draw_list->AddRectFilled(
            ImVec2(x1, line_y),
            ImVec2(x2, line_y + UITheme::dialogSeparatorThickness()),
            ImGui::ColorConvertFloat4ToU32(sep_color)
        );
        ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing() + UITheme::dialogSeparatorThickness()));
    }
}

void DialogWindow::renderOutlinedText(
    const char* text,
    const ImVec2& position,
    ImFont* font,
    float font_size_px,
    float wrap_width,
    float alpha_multiplier
)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 text_col_v4 = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    text_col_v4.w *= alpha_multiplier;
    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(text_col_v4);
    ImU32 outline_col = IM_COL32(0, 0, 0, (int)(text_col_v4.w * 255.0f));

    float thickness = std::clamp(std::round(font_size_px * 0.06f), 1.0f, 3.0f);

    for (int ox = -1; ox <= 1; ++ox)
    {
        for (int oy = -1; oy <= 1; ++oy)
        {
            if (ox == 0 && oy == 0) continue;
            dl->AddText(font, font_size_px, ImVec2(position.x + ox * thickness, position.y + oy * thickness), outline_col, text, nullptr, wrap_width);
        }
    }
    dl->AddText(font, font_size_px, position, text_col, text, nullptr, wrap_width);
}
