#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>
#include "../../utils/ErrorReporter.hpp"
#include "../DockState.hpp"
#include "../UIHelper.hpp"

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
#include "../../processing/NFKCTextNormalizer.hpp"
#include "../../config/ConfigManager.hpp"
#include "../UITheme.hpp"
#include "../../services/DQXClarityService.hpp"
#include "../../services/DQXClarityLauncher.hpp"
#include "../../dqxclarity/api/dialog_message.hpp"
#include "../../dqxclarity/api/corner_text.hpp"
#include "../../dqxclarity/api/dqxclarity.hpp"
#include "../Localization.hpp"

namespace
{
// Convert TargetLang enum to string code for glossary/translation
std::string toTargetCode(TranslationConfig::TargetLang lang)
{
    switch (lang)
    {
    case TranslationConfig::TargetLang::EN_US:
        return "en-US";
    case TranslationConfig::TargetLang::ZH_CN:
        return "zh-CN";
    case TranslationConfig::TargetLang::ZH_TW:
        return "zh-TW";
    }
    return "en-US";
}

// Prevents UTF-8 character corruption when copying to fixed-size buffers
// Walks backwards from truncation point to find safe character boundary
void safe_copy_utf8(char* dest, size_t dest_size, const std::string& src)
{
    if (dest_size == 0)
        return;
    if (src.empty())
    {
        dest[0] = '\0';
        return;
    }

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
    case TranslationConfig::TargetLang::EN_US:
        return "Waiting";
    case TranslationConfig::TargetLang::ZH_CN:
        return "等待中";
    case TranslationConfig::TargetLang::ZH_TW:
        return "等待中";
    }
    return "Waiting";
}

std::string strip_waiting_suffix(std::string text)
{
    auto ends_with = [](const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    const std::string ellipsis = "\xE2\x80\xA6"; // …
    const std::string full_stop = "\xE3\x80\x82"; // 。
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

bool isLLMBackend(TranslationConfig::TranslationBackend backend)
{
    switch (backend)
    {
    case TranslationConfig::TranslationBackend::OpenAI:
    case TranslationConfig::TranslationBackend::ZhipuGLM:
        return true;
    default:
        return false;
    }
}

bool translatorConfigIncomplete(const translate::BackendConfig& cfg, std::string& reason)
{
    using translate::Backend;
    switch (cfg.backend)
    {
    case Backend::OpenAI:
        if (cfg.api_key.empty() || cfg.model.empty() || cfg.base_url.empty())
        {
            reason = "OpenAI configuration requires base URL, model, and API key.";
            return true;
        }
        break;
    case Backend::Google:
        break;
    case Backend::ZhipuGLM:
        if (cfg.api_key.empty())
        {
            reason = "ZhipuGLM configuration requires an API key.";
            return true;
        }
        break;
    case Backend::QwenMT:
        if (cfg.api_key.empty())
        {
            reason = "Qwen MT configuration requires an API key.";
            return true;
        }
        break;
    case Backend::Niutrans:
        if (cfg.api_key.empty())
        {
            reason = "Niutrans configuration requires an API key.";
            return true;
        }
        break;
    case Backend::Youdao:
        if (cfg.api_key.empty() || cfg.api_secret.empty())
        {
            reason = "Youdao configuration requires app key and app secret.";
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}
} // namespace

void DialogWindow::ensurePlaceholderEntry()
{
    if (state_.content_state().segments.empty())
    {
        state_.content_state().segments.emplace_back();
        state_.content_state().segments.back().fill('\0');
    }
    if (state_.content_state().speakers.size() < state_.content_state().segments.size())
    {
        state_.content_state().speakers.resize(state_.content_state().segments.size());
    }
}

void DialogWindow::setPlaceholderText(const std::string& text, PlaceholderState state)
{
    ensurePlaceholderEntry();
    auto& buf = state_.content_state().segments[0];
    buf.fill('\0');
    safe_copy_utf8(buf.data(), buf.size(), text);
    if (!state_.content_state().speakers.empty())
    {
        state_.content_state().speakers[0].clear();
    }
    placeholder_active_ = true;
    placeholder_state_ = state;
    placeholder_base_text_ = text;
    activity_monitor_.markActive();
    if (state == PlaceholderState::Waiting)
    {
        animator_.reset();
    }
}

void DialogWindow::reinitializePlaceholder() { resetPlaceholder(); }

void DialogWindow::resetPlaceholder()
{
    setPlaceholderText(i18n::get("dialog.placeholder.waiting"), PlaceholderState::Waiting);
}

void DialogWindow::refreshPlaceholderStatus()
{
    if (!placeholder_active_)
    {
        return;
    }

    const std::string waiting = ui::LocalizedOrFallback("dialog.placeholder.waiting", "Initializing dialog system...");
    const std::string ready = ui::LocalizedOrFallback("dialog.placeholder.ready", "Dialog system ready.");
    const std::string failed = ui::LocalizedOrFallback(
        "dialog.placeholder.failed", "Dialog system failed to initialize. Check hook status and logs.");

    ensurePlaceholderEntry();

    auto set_if_changed = [&](const std::string& text, PlaceholderState state)
    {
        if (placeholder_state_ != state || placeholder_base_text_ != text)
        {
            setPlaceholderText(text, state);
        }
    };

    auto* launcher = DQXClarityService_Get();
    if (!launcher)
    {
        set_if_changed(waiting, PlaceholderState::Waiting);
        return;
    }

    auto stage = launcher->getEngineStage();
    if (stage == dqxclarity::Status::Hooked)
    {
        set_if_changed(ready, PlaceholderState::Ready);
    }
    else if (stage == dqxclarity::Status::Error)
    {
        set_if_changed(failed, PlaceholderState::Error);
    }
    else
    {
        set_if_changed(waiting, PlaceholderState::Waiting);
    }
}

int DialogWindow::appendSegmentInternal(const std::string& speaker, const std::string& text)
{
    std::string collapsed_text = text_normalizer_->collapseNewlines(text);
    if (placeholder_active_)
    {
        ensurePlaceholderEntry();
        safe_copy_utf8(state_.content_state().segments[0].data(), state_.content_state().segments[0].size(),
                       collapsed_text);
        if (!state_.content_state().speakers.empty())
        {
            state_.content_state().speakers[0] = speaker;
        }
        placeholder_active_ = false;
        placeholder_state_ = PlaceholderState::Ready;
        placeholder_base_text_.clear();
        activity_monitor_.markActive();
        return 0;
    }

    state_.content_state().segments.emplace_back();
    state_.content_state().segments.back().fill('\0');
    safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(),
                   collapsed_text);
    state_.content_state().speakers.push_back(speaker);
    activity_monitor_.markActive();
    return static_cast<int>(state_.content_state().segments.size()) - 1;
}

DialogWindow::DialogWindow(FontManager& font_manager, WindowRegistry& registry, ConfigManager& config, int instance_id, const std::string& name, bool is_default)
    : font_manager_(font_manager)
    , config_(config)
    , cached_backend_(translate::Backend::OpenAI)
    , settings_view_(state_, font_manager_, session_, config)
    , registry_(registry)
{

    name_ = name;
    id_suffix_ = "dialog_window_" + std::to_string(instance_id);
    settings_id_suffix_ = "dialog_settings_" + std::to_string(instance_id);
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
    is_default_instance_ = is_default;

    text_pipeline_ = std::make_unique<processing::TextPipeline>();
    text_normalizer_ = std::make_unique<processing::NFKCTextNormalizer>();

    state_.applyDefaults();

    resetPlaceholder();

    font_manager_.registerDialog(state_.ui_state());
}

DialogWindow::~DialogWindow() { font_manager_.unregisterDialog(state_.ui_state()); }

void DialogWindow::refreshFontBinding()
{
    // Re-assign active font and base size after external state replacement (e.g., config load)
    font_manager_.ensureFont(state_.ui_state());
}

void DialogWindow::applyPending()
{
    // Filter incoming messages to prevent empty translation requests
    auto is_blank = [](const std::string& s)
    {
        for (char c : s)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    };

    // Pull new dialog messages from in-process backlog (UI-layer merging)
    const TranslationConfig& config = activeTranslationConfig();
    const std::string corner_speaker_label = ui::LocalizedOrFallback("dialog.corner.speaker", "Follower Dialogue");

    if (auto* launcher = DQXClarityService_Get())
    {
        // Pull dialog messages (from DialogHook + DialogMemoryReader)
        if (config.include_dialog_stream)
        {
            std::vector<dqxclarity::DialogMessage> dialog_items;
            if (launcher->copyDialogsSince(last_applied_seq_, dialog_items))
            {
                for (auto& item : dialog_items)
                {
                    bool hasValidText = !item.text.empty() && !is_blank(item.text);
                    bool hasValidSpeaker = !item.speaker.empty() && item.speaker != "No_NPC";

                    if (hasValidText || hasValidSpeaker)
                    {
                        PendingMsg pm;
                        pm.is_corner_text = false;
                        pm.text = std::move(item.text);
                        pm.speaker = std::move(item.speaker);
                        pm.seq = item.seq;
                        pending_.push(std::move(pm));
                    }
                    last_applied_seq_ = std::max(last_applied_seq_, item.seq);
                }
            }
        }

        // Pull corner text messages separately (from CornerTextHook)
        if (config.include_corner_stream)
        {
            std::vector<dqxclarity::CornerTextItem> corner_items;
            if (launcher->copyCornerTextSince(last_corner_text_seq_, corner_items))
            {
                for (auto& item : corner_items)
                {
                    if (!item.text.empty() && !is_blank(item.text))
                    {
                        PendingMsg pm;
                        pm.is_corner_text = true;
                        pm.text = std::move(item.text);
                        pm.speaker = corner_speaker_label;
                        pm.seq = item.seq;
                        pending_.push(std::move(pm));
                    }
                    last_corner_text_seq_ = std::max(last_corner_text_seq_, item.seq);
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

    if (config.translate_enabled)
    {
        if (!translator_ || !translator_->isReady())
        {
            initTranslatorIfEnabled();
        }
    }

    for (auto& m : local)
    {
        std::string text_to_process = m.text;
        if (text_to_process.empty())
        {
            text_to_process = " ";
        }

        std::string target_lang_code = toTargetCode(config.target_lang_enum);
        bool use_glossary_replacement = config.glossary_enabled && !isLLMBackend(config.translation_backend);
        std::string processed_text =
            m.is_corner_text ?
                text_to_process :
                text_pipeline_->process(text_to_process, target_lang_code, use_glossary_replacement);

        if (processed_text.empty())
        {
            last_applied_seq_ = std::max(last_applied_seq_, m.seq);
            continue;
        }

        std::string speaker = m.speaker;
        if (speaker.empty() && m.is_corner_text)
        {
            speaker = corner_speaker_label;
        }

        if (config.translate_enabled)
        {
            auto backend_before = config.translation_backend;
            auto submit = session_.submit(processed_text, backend_before, config.target_lang_enum, translator_.get());

            if (submit.kind == TranslateSession::SubmitKind::Queued)
            {
                PLOG_INFO << "Queued translation job " << submit.job_id;
            }
            else if (submit.kind == TranslateSession::SubmitKind::DroppedNotReady)
            {
                PLOG_WARNING << "Dropped translation request (translator not ready); backend="
                             << static_cast<int>(backend_before);
            }
            else if (submit.kind == TranslateSession::SubmitKind::Cached)
            {
                PLOG_INFO << "Served translation from cache";
            }

            if (submit.kind == TranslateSession::SubmitKind::Cached)
            {
                appendSegmentInternal(speaker, submit.text);
                last_applied_seq_ = std::max(last_applied_seq_, m.seq);
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
                std::string placeholder = waiting_text_for_lang(config.target_lang_enum);
                placeholder += animator_.waitSuffix();
                int idx = appendSegmentInternal(speaker, placeholder);
                if (job_id != 0)
                {
                    pending_segment_by_job_[job_id] = idx;
                }
            }

            last_applied_seq_ = std::max(last_applied_seq_, m.seq);
        }
        else
        {
            appendSegmentInternal(speaker, text_to_process);
            last_applied_seq_ = std::max(last_applied_seq_, m.seq);
        }
    }
}

void DialogWindow::render()
{
    activity_monitor_.beginFrame();
    refreshPlaceholderStatus();
    applyPending();

    bool using_global = usingGlobalTranslation();
    if (using_global)
    {
        std::uint64_t version = config_.globalTranslationVersion();
        if (version != observed_global_translation_version_)
        {
            observed_global_translation_version_ = version;
            resetTranslatorState();
        }
    }
    else
    {
        if (last_used_global_translation_)
        {
            resetTranslatorState();
        }
        observed_global_translation_version_ = 0;
    }
    last_used_global_translation_ = using_global;

    // Process completed translations from background worker
    if (translator_)
    {
        std::vector<translate::Completed> done;
        if (translator_->drain(done))
        {
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
                            std::string failure_msg =
                                i18n::get_str("dialog.translate.timeout.failed") + " " + ev.original_text;
                            safe_copy_utf8(state_.content_state().segments[idx].data(),
                                           state_.content_state().segments[idx].size(), failure_msg);
                            failed_segments_.insert(idx);
                            failed_original_text_[idx] = ev.original_text;
                            failed_error_messages_[idx] = ev.error_message;
                            activity_monitor_.markActive();
                        }
                        else
                        {
                            std::string collapsed_text = text_normalizer_->collapseNewlines(ev.text);
                            safe_copy_utf8(state_.content_state().segments[idx].data(),
                                           state_.content_state().segments[idx].size(), collapsed_text);
                            failed_segments_.erase(idx);
                            failed_original_text_.erase(idx);
                            failed_error_messages_.erase(idx);
                            activity_monitor_.markActive();
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
                        std::string failure_msg =
                            i18n::get_str("dialog.translate.timeout.failed") + " " + ev.original_text;
                        safe_copy_utf8(state_.content_state().segments.back().data(),
                                       state_.content_state().segments.back().size(), failure_msg);
                        failed_segments_.insert(new_idx);
                        failed_original_text_[new_idx] = ev.original_text;
                        failed_error_messages_[new_idx] = ev.error_message;
                        activity_monitor_.markActive();
                    }
                    else
                    {
                        std::string collapsed_text = text_normalizer_->collapseNewlines(ev.text);
                        safe_copy_utf8(state_.content_state().segments.back().data(),
                                       state_.content_state().segments.back().size(), collapsed_text);
                        activity_monitor_.markActive();
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
    if (const char* err = config_.lastError(); err && err[0])
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err);
    }
    renderSettingsPanel();
}

void DialogWindow::renderDialog()
{
    ImGuiIO& io = ImGui::GetIO();
    const float max_dialog_width = std::max(400.0f, io.DisplaySize.x - 40.0f);
    const float max_dialog_height = std::max(400.0f, io.DisplaySize.y - 40.0f);

    const TranslationConfig& config = activeTranslationConfig();

    state_.ui_state().width = std::clamp(state_.ui_state().width, 400.0f, max_dialog_width);
    state_.ui_state().height = std::clamp(state_.ui_state().height, 400.0f, max_dialog_height);
    state_.ui_state().padding.x = std::clamp(state_.ui_state().padding.x, 4.0f, 80.0f);
    state_.ui_state().padding.y = std::clamp(state_.ui_state().padding.y, 4.0f, 80.0f);
    state_.ui_state().rounding = std::clamp(state_.ui_state().rounding, 0.0f, 32.0f);
    state_.ui_state().border_thickness = std::clamp(state_.ui_state().border_thickness, 0.5f, 6.0f);

    // Auto-fade logic (per-window setting)
    bool fade_enabled = state_.ui_state().fade_enabled;

    bool hover_reactivated = false;

    if (fade_enabled && state_.ui_state().current_alpha_multiplier <= 0.01f)
    {
        if (ImGui::IsMousePosValid(&io.MousePos))
        {
            ImVec2 cached_pos = state_.ui_state().window_pos;
            ImVec2 cached_size = state_.ui_state().window_size;
            if (cached_size.x > 0.0f && cached_size.y > 0.0f)
            {
                ImVec2 cached_max(cached_pos.x + cached_size.x, cached_pos.y + cached_size.y);
                if (ImGui::IsMouseHoveringRect(cached_pos, cached_max, false))
                {
                    state_.ui_state().last_activity_time = static_cast<float>(ImGui::GetTime());
                    state_.ui_state().current_alpha_multiplier = 1.0f;
                    hover_reactivated = true;
                }
            }
        }
    }

    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    else if (config_.globalState().appMode() == GlobalStateManager::AppMode::Mini)
    {
        ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
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

    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 400.0f), ImVec2(max_dialog_width, io.DisplaySize.y));

    const float fade_alpha = state_.ui_state().current_alpha_multiplier;
    float effective_alpha = state_.ui_state().background_alpha * fade_alpha;
    UITheme::pushDialogStyle(effective_alpha, state_.ui_state().padding, state_.ui_state().rounding,
                             state_.ui_state().border_thickness, state_.ui_state().border_enabled);
    const float style_alpha = std::max(fade_alpha, 0.001f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style_alpha);

    ImGuiWindowFlags dialog_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

    if (config_.globalState().appMode() == GlobalStateManager::AppMode::Mini)
    {
        dialog_flags |= ImGuiWindowFlags_NoMove;
    }

    if (ImGui::Begin(window_label_.c_str(), nullptr, dialog_flags))
    {
        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();

        // Check if mouse is hovering over the dialog window (fallback to rect hit when nearly transparent)
        bool is_hovered =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (!is_hovered && fade_enabled && fade_alpha < 0.99f)
        {
            ImVec2 window_max(window_pos.x + window_size.x, window_pos.y + window_size.y);
            is_hovered = ImGui::IsMouseHoveringRect(window_pos, window_max, false);
        }
        if (!hover_reactivated && is_hovered)
        {
            hover_reactivated = true;
        }
        activity_monitor_.setHover(hover_reactivated);

        renderVignette(window_pos, window_size, state_.ui_state().vignette_thickness, state_.ui_state().rounding,
                       state_.ui_state().current_alpha_multiplier);

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

        if (animate_placeholder)
        {
            ensurePlaceholderEntry();
            std::string base = placeholder_base_text_;
            if (base.empty())
            {
                base = ui::LocalizedOrFallback("dialog.placeholder.waiting", "Initializing dialog system...");
            }
            std::string trimmed = strip_waiting_suffix(base);
            const char* dots = animator_.waitSuffix();
            std::string composed;
            if (trimmed.empty())
                composed = dots;
            else
                composed = trimmed + dots;
            safe_copy_utf8(state_.content_state().segments[0].data(), state_.content_state().segments[0].size(),
                           composed);
        }

        // Update 'Waiting...' placeholder animation for in-flight translations
        if (animate_translations)
        {
            std::string base = waiting_text_for_lang(config.target_lang_enum);
            const char* dots = animator_.waitSuffix();
            for (const auto& kv : pending_segment_by_job_)
            {
                int idx = kv.second;
                if (idx >= 0 && idx < static_cast<int>(state_.content_state().segments.size()))
                {
                    std::string composed = base;
                    composed += dots;
                    safe_copy_utf8(state_.content_state().segments[idx].data(),
                                   state_.content_state().segments[idx].size(), composed);
                }
            }
        }

        // Filter out No_NPC and names containing corruption characters
        auto isValidNpcName = [](const std::string& name) -> bool
        {
            if (name.empty() || name == "No_NPC")
                return false;

            // Filter names containing corruption characters
            if (name.find('?') != std::string::npos || name.find('(') != std::string::npos ||
                name.find(')') != std::string::npos || name.find('<') != std::string::npos ||
                name.find('_') != std::string::npos || name.find('^') != std::string::npos ||
                name.find('>') != std::string::npos)
            {
                return false;
            }

            return true;
        };

        for (size_t i = 0; i < state_.content_state().segments.size(); ++i)
        {
            // Determine if we have a valid NPC name for this segment
            bool hasValidNpc = false;
            std::string currentSpeaker;
            if (i < state_.content_state().speakers.size())
            {
                currentSpeaker = state_.content_state().speakers[i];
                hasValidNpc = isValidNpcName(currentSpeaker);
            }

            ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
            ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
            float content_width = cr_max.x - cr_min.x;

            renderSeparator(hasValidNpc, currentSpeaker, content_width);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            const char* txt = state_.content_state().segments[i].data();

            bool placeholder_failed = (placeholder_active_ && placeholder_state_ == PlaceholderState::Error && i == 0);
            if (placeholder_failed)
            {
                ImVec4 err_color(1.0f, 0.4f, 0.3f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, err_color);
            }

            renderOutlinedText(txt, pos, ImGui::GetFont(), ImGui::GetFontSize(), wrap_width);

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
                    ImGui::TextColored(reason_color, "%s %s", reason_label.c_str(), err_it->second.c_str());
                    ImGui::Spacing();
                }

                std::string copy_btn_id =
                    std::string(i18n::get("dialog.translate.timeout.copy")) + "##copy_" + std::to_string(i);
                std::string retry_btn_id =
                    std::string(i18n::get("dialog.translate.timeout.retry")) + "##retry_" + std::to_string(i);

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
                        std::string target_lang_code = toTargetCode(config.target_lang_enum);
                        std::string processed_text = text_pipeline_->process(
                            text_to_retry, target_lang_code,
                            config.glossary_enabled && !isLLMBackend(config.translation_backend));
                        auto submit = session_.submit(processed_text, config.translation_backend,
                                                      config.target_lang_enum, translator_.get());

                        if (submit.kind == TranslateSession::SubmitKind::Queued && submit.job_id != 0)
                        {
                            pending_segment_by_job_[submit.job_id] = static_cast<int>(i);
                            std::string placeholder =
                                std::string(waiting_text_for_lang(config.target_lang_enum)) + " .";
                            safe_copy_utf8(state_.content_state().segments[i].data(),
                                           state_.content_state().segments[i].size(), placeholder);
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
        if (scroll_to_bottom_requested_)
        {
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
            scroll_to_bottom_requested_ = false;
        }

        animator_.update(state_.ui_state(), io.DeltaTime, activity_monitor_.isActive(),
                         activity_monitor_.hoverActive());

        const bool was_pending_resize = state_.ui_state().pending_resize;

        state_.ui_state().window_pos = ImGui::GetWindowPos();
        state_.ui_state().window_size = ImGui::GetWindowSize();

        if (!was_pending_resize)
        {
            state_.ui_state().width = state_.ui_state().window_size.x;
            state_.ui_state().height = state_.ui_state().window_size.y;
        }

        state_.ui_state().pending_reposition = false;
        state_.ui_state().pending_resize = false;
    }
    ImGui::End();

    ImGui::PopStyleVar();
    UITheme::popDialogStyle();
}

void DialogWindow::initTranslatorIfEnabled()
{
    const TranslationConfig& config = activeTranslationConfig();
    if (!config.translate_enabled)
    {
        resetTranslatorState();
        cached_translator_config_ = translate::BackendConfig{};
        translator_error_reported_ = false;
        return;
    }

    translate::BackendConfig cfg = translate::BackendConfig::from(config);

    std::string incomplete_reason;
    if (translatorConfigIncomplete(cfg, incomplete_reason))
    {
        if (!translator_error_reported_)
        {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Translation, utils::ErrorSeverity::Info,
                                              "Translator disabled: configuration incomplete", incomplete_reason);
            translator_error_reported_ = true;
        }
        resetTranslatorState();
        cached_translator_config_ = translate::BackendConfig{};
        translator_error_reported_ = true;
        return;
    }

    bool same_backend = translator_initialized_ && translator_ && cfg.backend == cached_backend_;
    bool same_config = same_backend && cfg.base_url == cached_translator_config_.base_url &&
                       cfg.model == cached_translator_config_.model &&
                       cfg.api_key == cached_translator_config_.api_key &&
                       cfg.api_secret == cached_translator_config_.api_secret &&
                       cfg.target_lang == cached_translator_config_.target_lang;

    if (same_config && translator_ && translator_->isReady())
    {
        translator_error_reported_ = false;
        return;
    }

    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }

    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg))
    {
        std::string details;
        if (translator_)
        {
            PLOG_WARNING << "Translator init failed for backend " << static_cast<int>(cfg.backend);
            if (const char* last = translator_->lastError())
                details = last;
            translator_->shutdown();
            translator_.reset();
        }
        else
        {
            PLOG_WARNING << "Translator factory returned null for backend " << static_cast<int>(cfg.backend);
        }

        if (!translator_error_reported_)
        {
            utils::ErrorReporter::ReportError(
                utils::ErrorCategory::Translation, utils::ErrorSeverity::Warning, "Translator failed to initialize",
                details.empty() ? (std::string("Backend index: ") + std::to_string(static_cast<int>(cfg.backend))) :
                                  details);
            translator_error_reported_ = true;
        }
        resetTranslatorState();
        cached_translator_config_ = translate::BackendConfig{};
        return;
    }

    if (!translator_->isReady())
    {
        PLOG_WARNING << "Translator not ready after init for backend " << static_cast<int>(cfg.backend);
        resetTranslatorState();
        cached_translator_config_ = translate::BackendConfig{};
        if (!translator_error_reported_)
        {
            std::string details;
            if (const char* last = translator_ ? translator_->lastError() : nullptr)
                details = last;
            utils::ErrorReporter::ReportError(
                utils::ErrorCategory::Translation, utils::ErrorSeverity::Warning, "Translator backend is not ready",
                details.empty() ? (std::string("Backend index: ") + std::to_string(static_cast<int>(cfg.backend))) :
                                  details);
            translator_error_reported_ = true;
        }
    }
    else
    {
        PLOG_INFO << "Translator ready for backend " << static_cast<int>(cfg.backend);
        cached_translator_config_ = cfg;
        cached_backend_ = cfg.backend;
        translator_initialized_ = true;
        translator_error_reported_ = false;
    }
}

void DialogWindow::renderSettingsPanel()
{
    settings_view_.render(
        translator_.get(), apply_hint_, apply_hint_timer_, testing_connection_, test_result_, test_timestamp_,
        settings_id_suffix_,
        [this]()
        {
            this->initTranslatorIfEnabled();
        },
        [this]() -> translate::ITranslator*
        {
            return translator_.get();
        });
}

const TranslationConfig& DialogWindow::activeTranslationConfig() const
{
    if (state_.use_global_translation)
    {
        return config_.globalTranslationConfig();
    }
    return state_.translation_config();
}

bool DialogWindow::usingGlobalTranslation() const
{
    return state_.use_global_translation;
}

void DialogWindow::resetTranslatorState()
{
    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }
    translator_initialized_ = false;
    cached_translator_config_ = translate::BackendConfig{};
    cached_backend_ = translate::Backend::OpenAI;
    translator_error_reported_ = false;
    pending_segment_by_job_.clear();
    failed_segments_.clear();
    failed_original_text_.clear();
    failed_error_messages_.clear();
}

// Handle right-click context menu for dialog window
void DialogWindow::renderDialogContextMenu()
{
    // Check if mouse is within this dialog window bounds
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    bool within_dialog =
        ImGui::IsMousePosValid(&mouse_pos) &&
        ImGui::IsMouseHoveringRect(state_.ui_state().window_pos,
                                   ImVec2(state_.ui_state().window_pos.x + state_.ui_state().window_size.x,
                                          state_.ui_state().window_pos.y + state_.ui_state().window_size.y),
                                   false);

    // Open context menu on right-click within dialog bounds
    if (within_dialog && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(("DialogContextMenu###" + id_suffix_).c_str());
    }

    // Use cached docked state from render
    bool is_docked = state_.ui_state().is_docked;

    // Get total dialog count from config manager registry
    int dialog_count = static_cast<int>(registry_.windowsByType(UIWindowType::Dialog).size());

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

        if (ImGui::MenuItem(ui::LocalizedOrFallback("dialog.context_menu.scroll_bottom", "Scroll to Bottom").c_str()))
        {
            scroll_to_bottom_requested_ = true;
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
                config_.requestShowGlobalSettings();
            }

            if (ImGui::BeginMenu(i18n::get("menu.app_mode")))
            {
                auto& gs = config_.globalState();
                auto mode = gs.appMode();
                bool sel_normal = (mode == GlobalStateManager::AppMode::Normal);
                bool sel_borderless = (mode == GlobalStateManager::AppMode::Borderless);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.normal"), nullptr, sel_normal))
                    gs.setAppMode(GlobalStateManager::AppMode::Normal);
                if (ImGui::MenuItem(i18n::get("settings.app_mode.items.borderless"), nullptr, sel_borderless))
                    gs.setAppMode(GlobalStateManager::AppMode::Borderless);
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(i18n::get("menu.quit")))
            {
                config_.requestQuit();
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
    std::string settings_title =
        name_ + " " + std::string(i18n::get("dialog.settings.window_suffix")) + "###" + settings_id_suffix_;
    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    else if (config_.globalState().appMode() == GlobalStateManager::AppMode::Mini)
    {
        ImGuiCond cond = DockState::ShouldReDock() ? ImGuiCond_Always : ImGuiCond_Once;
        ImGui::SetNextWindowDockID(DockState::GetDockspace(), cond);
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

void DialogWindow::renderVignette(const ImVec2& win_pos, const ImVec2& win_size, float thickness, float rounding,
                                  float alpha_multiplier)
{
    ui::RenderVignette(win_pos, win_size, thickness, rounding, alpha_multiplier);
}

void DialogWindow::renderSeparator(bool hasNPC, const std::string& speaker, float content_width)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
    ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
    float x1 = win_pos.x + cr_min.x;
    float x2 = win_pos.x + cr_max.x;

    ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing()));
    float y = ImGui::GetCursorScreenPos().y;
    float global_alpha = ImGui::GetStyle().Alpha;

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
            sep_color.w *= global_alpha;
            ImU32 sep_col_u32 = ImGui::ColorConvertFloat4ToU32(sep_color);

            draw_list->AddRectFilled(
                ImVec2(x1, line_y), ImVec2(x1 + line_width, line_y + UITheme::dialogSeparatorThickness()), sep_col_u32);
            draw_list->AddRectFilled(ImVec2(x2 - line_width, line_y),
                                     ImVec2(x2, line_y + UITheme::dialogSeparatorThickness()), sep_col_u32);
        }

        ImVec4 sep_text_color = UITheme::dialogSeparatorColor();
        sep_text_color.w *= global_alpha;
        ImVec2 text_pos((x1 + x2 - text_size.x) * 0.5f, y);

        ImU32 text_col = ImGui::ColorConvertFloat4ToU32(sep_text_color);
        ImU32 outline_col = IM_COL32(0, 0, 0, static_cast<int>(sep_text_color.w * 255.0f));
        float outline_offset = 1.0f;
        for (int ox = -1; ox <= 1; ++ox)
        {
            for (int oy = -1; oy <= 1; ++oy)
            {
                if (ox == 0 && oy == 0)
                    continue;
                draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                   ImVec2(text_pos.x + ox * outline_offset, text_pos.y + oy * outline_offset),
                                   outline_col, speaker.c_str());
            }
        }
        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, text_col, speaker.c_str());

        ImGui::Dummy(ImVec2(0.0f, text_size.y + UITheme::dialogSeparatorSpacing()));
    }
    else
    {
        float line_y = y;
        ImVec4 sep_color = UITheme::dialogSeparatorColor();
        sep_color.w *= global_alpha;
        draw_list->AddRectFilled(ImVec2(x1, line_y), ImVec2(x2, line_y + UITheme::dialogSeparatorThickness()),
                                 ImGui::ColorConvertFloat4ToU32(sep_color));
        ImGui::Dummy(ImVec2(0.0f, UITheme::dialogSeparatorSpacing() + UITheme::dialogSeparatorThickness()));
    }
}

void DialogWindow::renderOutlinedText(const char* text, const ImVec2& position, ImFont* font, float font_size_px,
                                      float wrap_width)
{
    ui::RenderOutlinedText(text, position, font, font_size_px, wrap_width);
}
