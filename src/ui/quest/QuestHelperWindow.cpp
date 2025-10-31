#include "QuestHelperWindow.hpp"
#include "QuestHelperSettingsView.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <plog/Log.h>

#include "config/ConfigManager.hpp"
#include "dqxclarity/api/quest_message.hpp"
#include "quest/QuestManager.hpp"
#include "services/DQXClarityService.hpp"
#include "services/DQXClarityLauncher.hpp"
#include "services/QuestManagerService.hpp"
#include "state/UIState.hpp"
#include "translate/ITranslator.hpp"
#include "ui/FontManager.hpp"
#include "ui/UIHelper.hpp"
#include "ui/UITheme.hpp"
#include "ui/Localization.hpp"
#include "ui/DockState.hpp"

using json = nlohmann::json;

QuestHelperWindow::QuestHelperWindow(FontManager& font_manager, const std::string& name)
    : font_manager_(font_manager)
    , name_(name)
{
    static int quest_helper_counter = 0;
    ++quest_helper_counter;
    id_suffix_ = "QuestHelper" + std::to_string(quest_helper_counter);
    settings_id_suffix_ = id_suffix_ + "_settings";
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;

    state_.applyDefaults();
    state_.ui.width = 500.0f;
    state_.ui.height = 600.0f;
    state_.ui.window_size = ImVec2(state_.ui.width, state_.ui.height);
    state_.ui.padding = ImVec2(16.0f, 16.0f);
    state_.ui.rounding = 8.0f;
    state_.ui.border_thickness = 2.0f;
    state_.ui.border_enabled = true;
    state_.ui.background_alpha = 0.85f;
    state_.ui.vignette_thickness = 18.0f;
    state_.ui.current_alpha_multiplier = 1.0f;

    session_.setCapacity(5000);
    session_.enableCache(true);

    settings_view_ = std::make_unique<QuestHelperSettingsView>(state_, font_manager_, session_);

    font_manager_.registerDialog(state_.ui);
    refreshFontBinding();
    initTranslatorIfEnabled();
}

QuestHelperWindow::~QuestHelperWindow()
{
    font_manager_.unregisterDialog(state_.ui);
    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }
}

void QuestHelperWindow::rename(const char* new_name)
{
    if (!new_name || !new_name[0])
        return;
    name_ = new_name;
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
}

void QuestHelperWindow::updateQuestData()
{
    auto* launcher = DQXClarityService_Get();
    if (!launcher)
        return;

    dqxclarity::QuestMessage msg;
    if (!launcher->getLatestQuest(msg))
        return;

    if (msg.seq == 0 || msg.seq == last_seq_)
        return;

    last_seq_ = msg.seq;

    if (msg.quest_name == current_quest_name_)
        return;

    current_quest_name_ = msg.quest_name;

    auto* quest_mgr = QuestManagerService_Get();
    if (!quest_mgr)
        return;

    auto quest_data = quest_mgr->findQuestByName(current_quest_name_);
    if (quest_data.has_value())
    {
        parseQuestJson(quest_data.value());
        activity_monitor_.markActive();
        submitTranslationRequest();
    }
    else
    {
        quest_id_.clear();
        quest_name_.clear();
        steps_.clear();
    }
}

void QuestHelperWindow::parseQuestJson(const std::string& jsonl)
{
    try
    {
        json obj = json::parse(jsonl);

        quest_id_ = obj.value("id", "");
        quest_name_ = obj.value("name", "");

        steps_.clear();

        if (obj.contains("steps") && obj["steps"].is_array())
        {
            for (const auto& step_json : obj["steps"])
            {
                QuestStep step;
                step.content = step_json.value("content", "");

                if (step_json.contains("komento") && step_json["komento"].is_array())
                {
                    for (const auto& k : step_json["komento"])
                    {
                        if (k.is_string())
                            step.komento.push_back(k.get<std::string>());
                    }
                }

                steps_.push_back(step);
            }
        }
    }
    catch (const json::exception& e)
    {
        PLOG_ERROR << "QuestHelperWindow: Failed to parse quest JSON: " << e.what();
        quest_id_.clear();
        quest_name_.clear();
        steps_.clear();
    }
}

void QuestHelperWindow::renderQuestContent(float wrap_width, float font_scale)
{
    (void)font_scale;

    if (!quest_id_.empty() && !quest_name_.empty())
    {
        std::string title = "【" + quest_id_ + "】" + quest_name_;

        const float title_font_scale = 1.5f;
        const float base_font_size = ImGui::GetFontSize();
        const float title_font_size = base_font_size * title_font_scale;

        ImVec2 title_size = ImGui::CalcTextSize(title.c_str(), nullptr, false, wrap_width);
        title_size.x *= title_font_scale;

        float center_offset = std::max(0.0f, (wrap_width - title_size.x) * 0.5f);
        ImVec2 title_pos = ImGui::GetCursorScreenPos();
        title_pos.x = ImGui::GetWindowPos().x + ImGui::GetCursorPosX() + center_offset;

        ui::RenderOutlinedText(title.c_str(), title_pos, ImGui::GetFont(), title_font_size, wrap_width);

        const float title_height = ImGui::GetTextLineHeightWithSpacing() * title_font_scale;
        ImGui::Dummy(ImVec2(0.0f, title_height));

        ImGui::Spacing();
        ImGui::Spacing();
    }

    const ImVec4 komento_color(171.0f / 255.0f, 139.0f / 255.0f, 96.0f / 255.0f, 1.0f);
    const float base_font_size = ImGui::GetFontSize();
    const auto& config = activeTranslationConfig();

    for (std::size_t i = 0; i < steps_.size(); ++i)
    {
        ui::DrawDefaultSeparator();
        ImGui::Spacing();

        // Determine what text to show for this step
        std::string step_text = steps_[i].content;
        if (config.translate_enabled && i < step_status_.size())
        {
            const StepStatus& status = step_status_[i];
            if (status.has_translation && !status.text.empty())
            {
                step_text = status.text;
            }
            else if (status.job_id != 0)
            {
                // Show "Waiting..." animation for pending translation
                std::string base = "Waiting";
                if (config.target_lang_enum == TranslationConfig::TargetLang::ZH_CN ||
                    config.target_lang_enum == TranslationConfig::TargetLang::ZH_TW)
                {
                    base = "等待中";
                }
                step_text = base + animator_.waitSuffix();
            }
        }

        ImVec2 step_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(step_text.c_str(), step_pos, ImGui::GetFont(), base_font_size, wrap_width);
        ImVec2 step_size = ImGui::CalcTextSize(step_text.c_str(), nullptr, false, wrap_width);
        ImGui::Dummy(ImVec2(0.0f, step_size.y));

        // Render komento with translations
        for (std::size_t k = 0; k < steps_[i].komento.size(); ++k)
        {
            std::string komento_text = steps_[i].komento[k];
            
            // Check for translated komento
            if (config.translate_enabled && i < step_status_.size())
            {
                const StepStatus& status = step_status_[i];
                if (k < status.komento_translations.size())
                {
                    if (!status.komento_translations[k].empty())
                    {
                        komento_text = status.komento_translations[k];
                    }
                    else if (k < status.komento_job_ids.size() && status.komento_job_ids[k] != 0)
                    {
                        // Show "Waiting..." animation for pending komento translation
                        std::string base = "Waiting";
                        if (config.target_lang_enum == TranslationConfig::TargetLang::ZH_CN ||
                            config.target_lang_enum == TranslationConfig::TargetLang::ZH_TW)
                        {
                            base = "等待中";
                        }
                        komento_text = base + animator_.waitSuffix();
                    }
                }
            }

            komento_text = "   " + komento_text;
            ImVec2 komento_pos = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Text, komento_color);
            ui::RenderOutlinedText(komento_text.c_str(), komento_pos, ImGui::GetFont(), base_font_size, wrap_width);
            ImGui::PopStyleColor();
            ImVec2 komento_size = ImGui::CalcTextSize(komento_text.c_str(), nullptr, false, wrap_width);
            ImGui::Dummy(ImVec2(0.0f, komento_size.y));
        }

        ImGui::Spacing();
    }
}

void QuestHelperWindow::renderContextMenu()
{
    if (state_.ui.window_size.x <= 0.0f || state_.ui.window_size.y <= 0.0f)
        return;

    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    bool within_window =
        ImGui::IsMousePosValid(&mouse_pos) &&
        ImGui::IsMouseHoveringRect(state_.ui.window_pos,
                                   ImVec2(state_.ui.window_pos.x + state_.ui.window_size.x,
                                          state_.ui.window_pos.y + state_.ui.window_size.y),
                                   false);

    std::string popup_id = "QuestHelperContextMenu###" + id_suffix_;

    if (within_window && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(popup_id.c_str());
    }

    if (ImGui::BeginPopup(popup_id.c_str()))
    {
        if (ImGui::MenuItem(ui::LocalizedOrFallback("common.settings", "Settings").c_str()))
        {
            show_settings_window_ = true;
        }

        ImGui::Separator();

        int quest_helper_count = 0;
        if (auto* cm = ConfigManager_Get())
        {
            if (auto* reg = cm->registry())
            {
                quest_helper_count = static_cast<int>(reg->windowsByType(UIWindowType::QuestHelper).size());
            }
        }
        bool can_remove = quest_helper_count > 1;
        if (ImGui::MenuItem(i18n::get("common.remove"), nullptr, false, can_remove))
        {
            should_be_removed_ = true;
        }

        ImGui::EndPopup();
    }
}

void QuestHelperWindow::render()
{
    activity_monitor_.beginFrame();
    updateQuestData();
    processTranslatorEvents();
    font_manager_.ensureFont(state_.ui);

    ImGuiIO& io = ImGui::GetIO();

    bool fade_enabled = state_.ui.fade_enabled;
    bool hover_active = false;

    if (fade_enabled && state_.ui.current_alpha_multiplier <= 0.01f)
    {
        if (ImGui::IsMousePosValid(&io.MousePos))
        {
            ImVec2 cached_pos = state_.ui.window_pos;
            ImVec2 cached_size = state_.ui.window_size;
            if (cached_size.x > 0.0f && cached_size.y > 0.0f)
            {
                ImVec2 cached_max(cached_pos.x + cached_size.x, cached_pos.y + cached_size.y);
                if (ImGui::IsMouseHoveringRect(cached_pos, cached_max, false))
                {
                    state_.ui.last_activity_time = static_cast<float>(ImGui::GetTime());
                    state_.ui.current_alpha_multiplier = 1.0f;
                    hover_active = true;
                }
            }
        }
    }

    if (state_.ui.pending_resize)
    {
        ImGui::SetNextWindowSize(ImVec2(state_.ui.width, state_.ui.height), ImGuiCond_Always);
    }
    else
    {
        ImGui::SetNextWindowSize(ImVec2(state_.ui.width, state_.ui.height), ImGuiCond_FirstUseEver);
    }

    const float fade_alpha = state_.ui.current_alpha_multiplier;
    float effective_alpha = state_.ui.background_alpha * fade_alpha;
    UITheme::pushDialogStyle(effective_alpha, state_.ui.padding, state_.ui.rounding,
                             state_.ui.border_thickness, state_.ui.border_enabled);
    const float style_alpha = std::max(fade_alpha, 0.001f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style_alpha);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

    bool window_open = true;
    if (ImGui::Begin(window_label_.c_str(), &window_open, flags))
    {
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();
        
        checkAndUpdateWindowHeight(win_size.x);

        bool is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (!is_hovered && fade_enabled && fade_alpha < 0.99f)
        {
            ImVec2 window_max(win_pos.x + win_size.x, win_pos.y + win_size.y);
            is_hovered = ImGui::IsMouseHoveringRect(win_pos, window_max, false);
        }
        if (!hover_active && is_hovered)
        {
            hover_active = true;
        }
        activity_monitor_.setHover(hover_active);

        ui::RenderVignette(win_pos, win_size, state_.ui.vignette_thickness, state_.ui.rounding,
                           state_.ui.current_alpha_multiplier);

        ImFont* active_font = state_.ui.font;
        float font_scale = 1.0f;
        if (active_font && state_.ui.font_base_size > 0.0f)
        {
            font_scale = std::max(0.3f, state_.ui.font_size / state_.ui.font_base_size);
        }
        if (active_font)
        {
            ImGui::PushFont(active_font);
            ImGui::SetWindowFontScale(font_scale);
        }

        const float wrap_width = std::max(60.0f, state_.ui.width - state_.ui.padding.x * 2.0f);
        renderQuestContent(wrap_width, font_scale);

        if (active_font)
        {
            ImGui::PopFont();
            ImGui::SetWindowFontScale(1.0f);
        }

        animator_.update(state_.ui, io.DeltaTime, activity_monitor_.isActive(),
                         activity_monitor_.hoverActive());

        state_.ui.window_pos = win_pos;
        state_.ui.window_size = win_size;

        // Update dimensions from actual window size
        state_.ui.width = win_size.x;
        
        // Only clear pending_resize if the window size matches our target
        if (state_.ui.pending_resize && std::abs(win_size.y - state_.ui.height) < 2.0f)
        {
            state_.ui.pending_resize = false;
        }
        
        // Update height from window if not resizing
        if (!state_.ui.pending_resize)
        {
            state_.ui.height = win_size.y;
        }

        state_.ui.pending_reposition = false;
        state_.ui.is_docked = ImGui::IsWindowDocked();
    }
    ImGui::End();

    ImGui::PopStyleVar();
    UITheme::popDialogStyle();

    if (!window_open)
        should_be_removed_ = true;

    renderContextMenu();
    renderSettingsWindow();
}

void QuestHelperWindow::renderSettings()
{
    show_settings_window_ = true;
}

void QuestHelperWindow::renderSettingsWindow()
{
    if (!show_settings_window_)
        return;

    ImGui::SetNextWindowSize(ImVec2(440.0f, 540.0f), ImGuiCond_FirstUseEver);
    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }

    if (ImGui::Begin(settings_window_label_.c_str(), &show_settings_window_))
    {
        auto initTranslatorFn = [this]() { initTranslatorIfEnabled(); };
        auto currentTranslatorFn = [this]() -> translate::ITranslator* { return translator_.get(); };
        settings_view_->render(translator_.get(), apply_hint_, apply_hint_timer_, testing_connection_,
                               test_result_, test_timestamp_, settings_id_suffix_, initTranslatorFn,
                               currentTranslatorFn);
    }
    ImGui::End();
}

const TranslationConfig& QuestHelperWindow::activeTranslationConfig() const
{
    if (state_.use_global_translation)
    {
        if (auto* cm = ConfigManager_Get())
        {
            return cm->globalTranslationConfig();
        }
    }
    return state_.translation_config();
}

bool QuestHelperWindow::usingGlobalTranslation() const
{
    return state_.use_global_translation;
}

void QuestHelperWindow::resetTranslatorState()
{
    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }
    translator_initialized_ = false;
    translator_error_reported_ = false;
}

void QuestHelperWindow::initTranslatorIfEnabled()
{
    const TranslationConfig& config = activeTranslationConfig();
    if (!config.translate_enabled)
    {
        resetTranslatorState();
        translator_error_reported_ = false;
        return;
    }

    translate::BackendConfig cfg = translate::BackendConfig::from(config);
    
    bool same_backend = translator_initialized_ && translator_ && cfg.backend == cached_backend_;
    bool same_config = same_backend && cfg.base_url == cached_config_.base_url && cfg.model == cached_config_.model &&
                       cfg.api_key == cached_config_.api_key && cfg.api_secret == cached_config_.api_secret &&
                       cfg.target_lang == cached_config_.target_lang;

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
        if (translator_)
        {
            translator_->shutdown();
            translator_.reset();
        }
        resetTranslatorState();
        return;
    }

    if (!translator_->isReady())
    {
        resetTranslatorState();
        return;
    }

    translator_initialized_ = true;
    cached_backend_ = cfg.backend;
    cached_config_ = cfg;
    translator_error_reported_ = false;
}

void QuestHelperWindow::refreshFontBinding()
{
    font_manager_.ensureFont(state_.ui);
}

void QuestHelperWindow::processTranslatorEvents()
{
    if (!translator_)
        return;

    std::vector<translate::Completed> done;
    if (!translator_->drain(done) || done.empty())
        return;

    std::vector<TranslateSession::CompletedEvent> events;
    session_.onCompleted(done, events);

    for (const auto& ev : events)
    {
        auto it = job_lookup_.find(ev.job_id);
        if (it == job_lookup_.end())
        {
            continue;
        }
        JobInfo job_info = it->second;
        job_lookup_.erase(it);

        if (job_info.step_index >= step_status_.size())
            continue;

        StepStatus& status = step_status_[job_info.step_index];

        if (job_info.komento_index == SIZE_MAX)
        {
            // Step content translation
            if (ev.failed)
            {
                handleTranslationFailure(job_info.step_index,
                                         ev.error_message.empty() ?
                                             ui::LocalizedOrFallback("quest.translation.error", "Translation failed") :
                                             ev.error_message);
            }
            else
            {
                applyCachedTranslation(job_info.step_index, ev.text);
            }
        }
        else
        {
            // Komento translation
            if (job_info.komento_index < status.komento_translations.size())
            {
                if (!ev.failed)
                {
                    status.komento_translations[job_info.komento_index] = ev.text;
                }
                status.komento_job_ids[job_info.komento_index] = 0;
            }
        }
    }
}

void QuestHelperWindow::submitTranslationRequest()
{
    if (!translator_ || !translator_->isReady())
        return;

    const auto& config = activeTranslationConfig();
    if (!config.translate_enabled)
        return;

    // Clear previous translations
    step_status_.clear();
    step_status_.resize(steps_.size());
    job_lookup_.clear();

    // Submit translation for each step and its komento
    for (std::size_t i = 0; i < steps_.size(); ++i)
    {
        // Submit step content translation
        submitStepTranslation(i, steps_[i].content, config);

        // Initialize komento translation arrays
        step_status_[i].komento_translations.resize(steps_[i].komento.size());
        step_status_[i].komento_job_ids.resize(steps_[i].komento.size(), 0);

        // Submit each komento translation
        for (std::size_t k = 0; k < steps_[i].komento.size(); ++k)
        {
            if (steps_[i].komento[k].empty())
            {
                step_status_[i].komento_translations[k] = "";
                continue;
            }

            auto submit = session_.submit(steps_[i].komento[k], config.translation_backend, config.target_lang_enum, translator_.get());

            if (submit.kind == TranslateSession::SubmitKind::Cached)
            {
                step_status_[i].komento_translations[k] = submit.text;
            }
            else if (submit.kind == TranslateSession::SubmitKind::Queued && submit.job_id != 0)
            {
                step_status_[i].komento_job_ids[k] = submit.job_id;
                job_lookup_[submit.job_id] = JobInfo{i, k};
            }
        }
    }
}

void QuestHelperWindow::submitStepTranslation(std::size_t step_index, const std::string& text,
                                               const TranslationConfig& config)
{
    if (step_index >= step_status_.size())
        return;

    StepStatus& status = step_status_[step_index];
    status = StepStatus{};

    if (text.empty())
    {
        status.has_translation = true;
        status.failed = false;
        return;
    }

    auto submit = session_.submit(text, config.translation_backend, config.target_lang_enum, translator_.get());

    if (submit.kind == TranslateSession::SubmitKind::Cached)
    {
        applyCachedTranslation(step_index, submit.text);
        return;
    }

    if (submit.kind == TranslateSession::SubmitKind::Queued && submit.job_id != 0)
    {
        status.job_id = submit.job_id;
        job_lookup_[submit.job_id] = JobInfo{step_index, SIZE_MAX};
        return;
    }

    handleTranslationFailure(step_index,
                              ui::LocalizedOrFallback("quest.translation.queue_failed", "Unable to queue translation request."));
}

void QuestHelperWindow::applyCachedTranslation(std::size_t step_index, const std::string& text)
{
    if (step_index >= step_status_.size())
        return;

    StepStatus& status = step_status_[step_index];
    status.has_translation = true;
    status.failed = false;
    status.error.clear();
    status.job_id = 0;
    status.text = text;
}

void QuestHelperWindow::handleTranslationFailure(std::size_t step_index, const std::string& message)
{
    if (step_index >= step_status_.size())
        return;

    StepStatus& status = step_status_[step_index];
    status.has_translation = false;
    status.failed = true;
    status.error = message;
    status.job_id = 0;
}

float QuestHelperWindow::calculateContentHeight(float wrap_width, float font_scale) const
{
    float total_height = 0.0f;
    const auto& config = activeTranslationConfig();

    // Title height
    if (!quest_id_.empty() && !quest_name_.empty())
    {
        const float title_font_scale = 1.5f;
        const float title_height = (ImGui::GetTextLineHeightWithSpacing() * font_scale) * title_font_scale;
        total_height += title_height;
        total_height += ImGui::GetStyle().ItemSpacing.y * 2; // Two spacings
    }

    // Steps height
    for (std::size_t i = 0; i < steps_.size(); ++i)
    {
        // Separator
        total_height += ImGui::GetStyle().ItemSpacing.y;
        total_height += 1.0f; // Separator line
        total_height += ImGui::GetStyle().ItemSpacing.y;

        // Step content
        std::string step_text = steps_[i].content;
        if (config.translate_enabled && i < step_status_.size())
        {
            const StepStatus& status = step_status_[i];
            if (status.has_translation && !status.text.empty())
            {
                step_text = status.text;
            }
            else if (status.job_id != 0)
            {
                step_text = "Waiting...";
            }
        }
        ImVec2 step_size = ImGui::CalcTextSize(step_text.c_str(), nullptr, false, wrap_width / font_scale);
        total_height += step_size.y * font_scale;

        // Komento height
        for (std::size_t k = 0; k < steps_[i].komento.size(); ++k)
        {
            std::string komento_text = steps_[i].komento[k];
            if (config.translate_enabled && i < step_status_.size())
            {
                const StepStatus& status = step_status_[i];
                if (k < status.komento_translations.size())
                {
                    if (!status.komento_translations[k].empty())
                    {
                        komento_text = status.komento_translations[k];
                    }
                    else if (k < status.komento_job_ids.size() && status.komento_job_ids[k] != 0)
                    {
                        komento_text = "Waiting...";
                    }
                }
            }
            komento_text = "   " + komento_text;
            ImVec2 komento_size = ImGui::CalcTextSize(komento_text.c_str(), nullptr, false, wrap_width / font_scale);
            total_height += komento_size.y * font_scale;
        }

        total_height += ImGui::GetStyle().ItemSpacing.y;
    }

    // Add padding
    total_height += state_.ui.padding.y * 2.0f;
    
    return total_height;
}

void QuestHelperWindow::checkAndUpdateWindowHeight(float current_window_width)
{
    const auto& config = activeTranslationConfig();
    
    // Calculate content hash to detect changes
    std::size_t content_hash = steps_.size();
    for (const auto& step : steps_)
    {
        content_hash ^= std::hash<std::string>{}(step.content);
        for (const auto& k : step.komento)
        {
            content_hash ^= std::hash<std::string>{}(k);
        }
    }
    
    // Add translation state to hash (including job IDs to detect when translations arrive)
    if (config.translate_enabled)
    {
        for (const auto& status : step_status_)
        {
            // Hash the translation text
            if (status.has_translation)
            {
                content_hash ^= std::hash<std::string>{}(status.text);
            }
            // Hash job ID to detect when it changes (translation arrives)
            content_hash ^= status.job_id;
            
            // Hash komento translations
            for (const auto& kt : status.komento_translations)
            {
                content_hash ^= std::hash<std::string>{}(kt);
            }
            // Hash komento job IDs
            for (const auto& kid : status.komento_job_ids)
            {
                content_hash ^= kid;
            }
        }
    }
    
    // Check if we need to resize
    bool content_changed = (content_hash != last_content_hash_);
    bool font_changed = (state_.ui.font_size != last_font_size_);
    
    if (content_changed || font_changed)
    {
        const float wrap_width = std::max(60.0f, current_window_width - state_.ui.padding.x * 2.0f);
        float font_scale = 1.0f;
        if (state_.ui.font && state_.ui.font_base_size > 0.0f)
        {
            font_scale = std::max(0.3f, state_.ui.font_size / state_.ui.font_base_size);
        }
        
        float required_height = calculateContentHeight(wrap_width, font_scale);
        
        // Add window frame height (title bar + borders)
        ImGuiStyle& style = ImGui::GetStyle();
        required_height += style.FramePadding.y * 2.0f;
        required_height += ImGui::GetFrameHeight();
        
        // Add some margin
        required_height += 10.0f;
        
        // Clamp to reasonable bounds
        required_height = std::max(200.0f, std::min(required_height, 1200.0f));
        
        if (std::abs(required_height - state_.ui.height) > 5.0f)
        {
            state_.ui.height = required_height;
            state_.ui.window_size.y = required_height;
            state_.ui.pending_resize = true;
        }
        else
        {
            // Height already matches - update hash
            last_content_hash_ = content_hash;
            last_font_size_ = state_.ui.font_size;
        }
    }
    else if (!state_.ui.pending_resize)
    {
        // No change needed - update hash
        last_content_hash_ = content_hash;
        last_font_size_ = state_.ui.font_size;
    }
}
