#include "DialogWindow.hpp"

#include <imgui.h>
#include <plog/Log.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "IconUtils.hpp"
#include "translate/ITranslator.hpp"
#include "translate/LabelProcessor.hpp"
#include "config/ConfigManager.hpp"
#include "UITheme.hpp"
#include "DQXClarityService.hpp"
#include "DQXClarityLauncher.hpp"
#include "dqxclarity/api/dialog_message.hpp"

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

    state_.ui_state().font_path.fill('\0');
    state_.content_state().append_buffer.fill('\0');
    state_.content_state().segments.emplace_back();
    safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), reinterpret_cast<const char*>(u8""));
    // Legacy TCP portfile path no longer used (Phase 3)
    state_.ipc_config().portfile_path.fill('\0');
    state_.translation_config().target_lang_enum = TranslationConfig::TargetLang::EN_US;
    state_.translation_config().openai_base_url.fill('\0');
    std::snprintf(state_.translation_config().openai_base_url.data(), state_.translation_config().openai_base_url.size(), "%s", "https://api.openai.com");
    state_.translation_config().openai_model.fill('\0');
    state_.translation_config().openai_api_key.fill('\0');

    font_manager_.registerDialog(state_.ui_state());
}

DialogWindow::~DialogWindow()
{
    font_manager_.unregisterDialog(state_.ui_state());
}

void DialogWindow::applyPending()
{
    // Filter incoming messages to prevent empty translation requests
    auto is_blank = [](const std::string& s) {
        for (char c : s) { if (!std::isspace(static_cast<unsigned char>(c))) return false; }
        return true;
    };

    // Pull new dialog messages from in-process backlog (Phase 3)
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
            if (translator_ && translator_->isReady())
            {
                // Process labels before translation
                std::string processed_text = label_processor_->processText(m.text);
                
                // Queue async translation job - original text will be replaced by translation
                std::uint64_t job_id = 0;
                std::string target_lang_str;
                switch (state_.translation_config().target_lang_enum)
                {
                case TranslationConfig::TargetLang::EN_US: target_lang_str = "en-us"; break;
                case TranslationConfig::TargetLang::ZH_CN: target_lang_str = "zh-cn"; break;
                case TranslationConfig::TargetLang::ZH_TW: target_lang_str = "zh-tw"; break;
                }
                translator_->translate(processed_text, "auto", target_lang_str, job_id);
                last_job_id_ = job_id;
            }
            // Skip showing original text - only show translation when ready
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
            for (auto& r : done)
            {
                state_.content_state().segments.emplace_back();
                safe_copy_utf8(state_.content_state().segments.back().data(), state_.content_state().segments.back().size(), r.text);
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
        for (size_t i = 0; i < state_.content_state().segments.size(); ++i)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
            ImGui::TextUnformatted(state_.content_state().segments[i].data());
            ImGui::PopTextWrapPos();
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

        // Auto-scroll to bottom when new content is appended
        if (state_.ipc_config().auto_scroll_to_new && appended_since_last_frame_)
        {
            ImGui::SetScrollHereY(1.0f);
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
    cfg.base_url = state_.translation_config().openai_base_url.data();
    cfg.model = state_.translation_config().openai_model.data();
    if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
    {
        cfg.api_key = state_.translation_config().openai_api_key.data();
    }
    else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
    {
        cfg.api_key = state_.translation_config().google_api_key.data();
    }
    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg))
    {
        translator_.reset();
    }
}

void DialogWindow::autoConnectIPC()
{
    // Phase 3: no-op (legacy TCP IPC removed)
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
    if (ImGui::Button("Save Config"))
    {
        extern bool ConfigManager_SaveAll();
        bool ok = ConfigManager_SaveAll();
        if (!ok)
        {
            ImGui::SameLine();
            ImGui::TextColored(UITheme::warningColor(), "%s", "Failed to save config; see logs.");
        }
    }
    ImGui::Spacing();

    // APPEARANCE SECTION
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        
        ImGui::Checkbox("Auto-scroll to new", &state_.ipc_config().auto_scroll_to_new);
        ImGui::Spacing();

        ImGui::TextUnformatted("Dialog Width");
        set_slider_width();
        width_changed = ImGui::SliderFloat("##dialog_width_slider", &state_.ui_state().width, 200.0f, max_dialog_width);
        ImGui::Spacing();

        ImGui::TextUnformatted("Dialog Height");
        set_slider_width();
        height_changed = ImGui::SliderFloat("##dialog_height_slider", &state_.ui_state().height, 80.0f, max_dialog_height);
        ImGui::Spacing();

        ImGui::TextUnformatted("Padding XY");
        set_slider_width();
        ImGui::SliderFloat2("##dialog_padding_slider", &state_.ui_state().padding.x, 4.0f, 80.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted("Corner Rounding");
        set_slider_width();
        ImGui::SliderFloat("##dialog_rounding_slider", &state_.ui_state().rounding, 0.0f, 32.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted("Border Thickness");
        set_slider_width();
        ImGui::SliderFloat("##dialog_border_slider", &state_.ui_state().border_thickness, 0.5f, 6.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted("Background Opacity");
        set_slider_width();
        alpha_changed = ImGui::SliderFloat("##dialog_bg_alpha_slider", &state_.ui_state().background_alpha, 0.0f, 1.0f);
        ImGui::Spacing();

        ImGui::TextUnformatted("Font Size");
        set_slider_width();
        float min_font = std::max(8.0f, state_.ui_state().font_base_size * 0.5f);
        float max_font = state_.ui_state().font_base_size * 2.5f;
        font_changed = ImGui::SliderFloat("##dialog_font_size_slider", &state_.ui_state().font_size, min_font, max_font);
        
        ImGui::Unindent();
        ImGui::Spacing();
    }

    // TRANSLATE SECTION
    if (ImGui::CollapsingHeader("Translate", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        
        ImGui::Checkbox("Enable Translation", &state_.translation_config().translate_enabled);
        
        ImGui::TextUnformatted("Backend");
        const char* backend_items[] = { "OpenAI-compatible", "Google Translate" };
        int current_backend = static_cast<int>(state_.translation_config().translation_backend);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##translation_backend", &current_backend, backend_items, IM_ARRAYSIZE(backend_items)))
        {
            state_.translation_config().translation_backend = static_cast<TranslationConfig::TranslationBackend>(current_backend);
        }
        
        ImGui::TextUnformatted("Target Language");
        const char* lang_items[] = { "English (US)", "Chinese (Simplified)", "Chinese (Traditional)" };
        int current_lang = static_cast<int>(state_.translation_config().target_lang_enum);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##target_lang", &current_lang, lang_items, IM_ARRAYSIZE(lang_items)))
        {
            state_.translation_config().target_lang_enum = static_cast<TranslationConfig::TargetLang>(current_lang);
        }

        // Show backend-specific configuration
        if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
        {
            ImGui::TextUnformatted("Base URL");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputText("##openai_base", state_.translation_config().openai_base_url.data(), state_.translation_config().openai_base_url.size());
            ImGui::TextDisabled("Examples: https://api.openai.com, http://localhost:8000, http://127.0.0.1:11434/v1");

            ImGui::TextUnformatted("Model");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputText("##openai_model", state_.translation_config().openai_model.data(), state_.translation_config().openai_model.size());

            ImGui::TextUnformatted("API Key");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputText("##openai_key", state_.translation_config().openai_api_key.data(), state_.translation_config().openai_api_key.size(), ImGuiInputTextFlags_Password);
        }
        else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
        {
            ImGui::TextUnformatted("API Key (Optional)");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputText("##google_key", state_.translation_config().google_api_key.data(), state_.translation_config().google_api_key.size(), ImGuiInputTextFlags_Password);
            ImGui::TextDisabled("Leave empty to use free tier. Paid API requires Google Cloud credentials.");
        }

        bool ready = translator_ && translator_->isReady();
        if (!ready)
        {
            if (ImGui::Button("Apply"))
            {
                initTranslatorIfEnabled();
            }
            ImGui::SameLine();
            if (ImGui::Button("Test") && !testing_connection_)
            {
                testing_connection_ = true;
                test_result_ = "Testing connection...";
                // Create temporary translator for testing
                translate::TranslatorConfig test_cfg;
                test_cfg.backend = static_cast<translate::Backend>(state_.translation_config().translation_backend);
                switch (state_.translation_config().target_lang_enum)
                {
                case TranslationConfig::TargetLang::EN_US: test_cfg.target_lang = "en-us"; break;
                case TranslationConfig::TargetLang::ZH_CN: test_cfg.target_lang = "zh-cn"; break;
                case TranslationConfig::TargetLang::ZH_TW: test_cfg.target_lang = "zh-tw"; break;
                }
                test_cfg.base_url = state_.translation_config().openai_base_url.data();
                test_cfg.model = state_.translation_config().openai_model.data();
                if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::OpenAI)
                {
                    test_cfg.api_key = state_.translation_config().openai_api_key.data();
                }
                else if (state_.translation_config().translation_backend == TranslationConfig::TranslationBackend::Google)
                {
                    test_cfg.api_key = state_.translation_config().google_api_key.data();
                }
                
                auto temp_translator = translate::createTranslator(test_cfg.backend);
                if (temp_translator && temp_translator->init(test_cfg))
                {
                    test_result_ = temp_translator->testConnection();
                }
                else
                {
                    test_result_ = "Error: Failed to initialize translator for testing";
                }
                if (temp_translator) temp_translator->shutdown();
                testing_connection_ = false;
            }
        }
        else
        {
            if (ImGui::Button("Stop"))
            {
                translator_->shutdown();
                translator_.reset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Test") && !testing_connection_)
            {
                testing_connection_ = true;
                test_result_ = "Testing connection...";
                test_result_ = translator_->testConnection();
                testing_connection_ = false;
            }
        }

        const char* status = (translator_ && translator_->isReady()) ? "Ready" : "Not Ready";
        ImGui::SameLine(); ImGui::TextDisabled("Status: %s", status);
        ImGui::SameLine(); if (ImGui::SmallButton("Refresh")) initTranslatorIfEnabled();
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
            
            ImGui::TextColored(color, "%s", test_result_.c_str());
            if (ImGui::SmallButton("Clear Test Result"))
            {
                test_result_.clear();
            }
        }
        
        ImGui::Unindent();
        ImGui::Spacing();
    }

    renderStatusSection();

    // DEBUG SECTION
    if (ImGui::CollapsingHeader("Debug"))
    {
        ImGui::Indent();
        ImGui::PushID(settings_id_suffix_.c_str());
        
        // IPC Section (Phase 3): in-process
        ImGui::TextUnformatted("Text Source: In-Process Ring Buffer");
        ImGui::TextDisabled("Delivery: Start/Stop controls, 5s delayed auto-start on DQXGame.exe detection.");
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Font Section
        ImGui::TextUnformatted("Font Path");
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float avail = ImGui::GetContentRegionAvail().x;
            float btn_w = ImGui::CalcTextSize("Reload Font").x + style.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(220.0f, avail - btn_w - style.ItemSpacing.x));
            ImGui::InputText("##font_path", state_.ui_state().font_path.data(), state_.ui_state().font_path.size());
            ImGui::SameLine();
            if (ImGui::Button("Reload Font"))
            {
                bool loaded = font_manager_.reloadFont(state_.ui_state().font_path.data());
                state_.ui_state().has_custom_font = loaded;
            }
            ImGui::TextDisabled("Active font: %s", state_.ui_state().has_custom_font ? "custom" : "default (ASCII only)");
            if (!state_.ui_state().has_custom_font)
                ImGui::TextColored(UITheme::warningColor(), "No CJK font loaded; Japanese text may appear as '?' characters.");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Dialog Texts Section
        ImGui::TextUnformatted("Appended Texts");
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
                if (ImGui::SmallButton("Edit"))
                {
                    state_.content_state().editing_index = i;
                    std::snprintf(state_.content_state().edit_buffer.data(), state_.content_state().edit_buffer.size(), "%s", state_.content_state().segments[i].data());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
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
            ImGui::TextDisabled("Editing Entry #%d", state_.content_state().editing_index);
            ImVec2 box(0, 160.0f);
            ImGui::InputTextMultiline("##full_editor", state_.content_state().edit_buffer.data(), state_.content_state().edit_buffer.size(), box);
            if (ImGui::Button("Save"))
            {
                // Save back to segment (truncate to buffer size)
                safe_copy_utf8(state_.content_state().segments[state_.content_state().editing_index].data(), state_.content_state().segments[state_.content_state().editing_index].size(), state_.content_state().edit_buffer.data());
                state_.content_state().editing_index = -1;
                state_.content_state().edit_buffer[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                state_.content_state().editing_index = -1;
                state_.content_state().edit_buffer[0] = '\0';
            }
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Append New Text");
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float append_avail = ImGui::GetContentRegionAvail().x;
            float btn_w = ImGui::CalcTextSize("Append").x + style.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(std::max(220.0f, append_avail - btn_w - style.ItemSpacing.x));
            ImGui::InputText("##append", state_.content_state().append_buffer.data(), state_.content_state().append_buffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Append"))
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
        if (ImGui::MenuItem("Settings"))
        {
            show_settings_window_ = !show_settings_window_;
        }
        
        if (ImGui::MenuItem("Remove"))
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

void DialogWindow::renderStatusSection()
{
    if (ImGui::CollapsingHeader("Status"))
    {
        ImGui::Indent();

        ImGui::TextUnformatted("Translation:");
        ImGui::SameLine();
        if (!state_.translation_config().translate_enabled)
        {
            ImGui::TextColored(UITheme::disabledColor(), "● Disabled");
        }
        else if (translator_ && translator_->isReady())
        {
            ImGui::TextColored(UITheme::successColor(), "● OK");
        }
        else
        {
            const char* error_msg = (translator_ && translator_->lastError() && translator_->lastError()[0]) ? translator_->lastError() : "Not Ready";
            ImGui::TextColored(UITheme::errorColor(), "● %s", error_msg);
        }

        ImGui::TextUnformatted("Delivery:");
        ImGui::SameLine();
        ImGui::TextColored(UITheme::successColor(), "● In-Process");

        ImGui::Unindent();
        ImGui::Spacing();
    }
}
