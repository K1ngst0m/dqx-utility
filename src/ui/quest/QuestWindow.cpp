#include "QuestWindow.hpp"

#include <imgui.h>

#include "../UIHelper.hpp"
#include "../Localization.hpp"
#include "../UITheme.hpp"
#include "../DockState.hpp"
#include "../FontManager.hpp"
#include "QuestSettingsView.hpp"
#include "../../services/DQXClarityLauncher.hpp"
#include "../../services/DQXClarityService.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../translate/ITranslator.hpp"
#include "../../translate/TranslateSession.hpp"
#include "../../dqxclarity/api/quest_message.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>
#include <vector>
#include <plog/Log.h>

#include "../../utils/ErrorReporter.hpp"


namespace {

const std::string kFullWidthSpace = "\xE3\x80\x80"; // full-width space
const std::string kBullet = "\xE3\x83\xBB";         // ・
const std::string kMultiplierSymbol = "\xC3\x97";     // ×
const std::array<std::string, 3> kCountSuffixes = {
    std::string("\xE3\x81\x93"), // こ
    std::string("\xE5\x80\x8B"), // 個
    std::string("\xE4\xB8\xAA")  // 个
};

bool isFullWidthDigitAt(const std::string& text, size_t index)
{
    if (index + 3 > text.size())
        return false;
    unsigned char c1 = static_cast<unsigned char>(text[index]);
    unsigned char c2 = static_cast<unsigned char>(text[index + 1]);
    unsigned char c3 = static_cast<unsigned char>(text[index + 2]);
    return c1 == 0xEF && c2 == 0xBC && c3 >= 0x90 && c3 <= 0x99;
}

bool isFullWidthSpaceAt(const std::string& text, size_t index)
{
    return index + kFullWidthSpace.size() <= text.size() &&
        text.compare(index, kFullWidthSpace.size(), kFullWidthSpace) == 0;
}

void skipTrailingSpaces(const std::string& text, size_t& pos)
{
    while (pos > 0)
    {
        if (pos >= 3 && isFullWidthSpaceAt(text, pos - 3))
        {
            pos -= 3;
            continue;
        }

        unsigned char c = static_cast<unsigned char>(text[pos - 1]);
        if (c == ' ' || c == '\t')
        {
            --pos;
            continue;
        }
        break;
    }
}

bool collectTrailingDigits(const std::string& text, size_t& pos, std::string& digits)
{
    size_t scan = pos;
    digits.clear();

    while (scan > 0)
    {
        unsigned char c = static_cast<unsigned char>(text[scan - 1]);
        if (c >= '0' && c <= '9')
        {
            digits.insert(digits.begin(), static_cast<char>(c));
            --scan;
            continue;
        }

        if (scan >= 3 && isFullWidthDigitAt(text, scan - 3))
        {
            unsigned char c3 = static_cast<unsigned char>(text[scan - 1]);
            digits.insert(digits.begin(), static_cast<char>('0' + (c3 - 0x90)));
            scan -= 3;
            continue;
        }

        break;
    }

    pos = scan;
    return !digits.empty();
}

bool tryParseTrailingCountSuffix(const std::string& text, size_t end_pos, size_t& begin_out, int& count_out)
{
    size_t pos = end_pos;
    skipTrailingSpaces(text, pos);
    size_t trimmed_end = pos;

    for (const auto& suffix : kCountSuffixes)
    {
        if (trimmed_end < suffix.size())
            continue;

        if (text.compare(trimmed_end - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        size_t digits_end = trimmed_end - suffix.size();
        size_t digits_begin = digits_end;
        std::string digits;
        if (!collectTrailingDigits(text, digits_begin, digits))
            continue;

        try
        {
            int value = std::stoi(digits);
            if (value > 0)
            {
                begin_out = digits_begin;
                count_out = value;
                return true;
            }
        }
        catch (...)
        {
        }
    }

    return false;
}

bool tryParseTrailingMultiplier(const std::string& text, size_t end_pos, size_t& begin_out, int& count_out)
{
    size_t pos = end_pos;
    skipTrailingSpaces(text, pos);

    size_t digits_begin = pos;
    std::string digits;
    if (!collectTrailingDigits(text, digits_begin, digits))
        return false;

    size_t symbol_pos = digits_begin;
    skipTrailingSpaces(text, symbol_pos);
    if (symbol_pos == 0)
        return false;

    auto parseDigitsValue = [&]() -> int {
        try
        {
            return std::stoi(digits);
        }
        catch (...)
        {
            return 0;
        }
    };

    if (symbol_pos >= 2)
    {
        unsigned char c1 = static_cast<unsigned char>(text[symbol_pos - 2]);
        unsigned char c2 = static_cast<unsigned char>(text[symbol_pos - 1]);
        if (c1 == 0xC3 && c2 == 0x97) // ×
        {
            int value = parseDigitsValue();
            if (value > 0)
            {
                begin_out = symbol_pos - 2;
                count_out = value;
                return true;
            }
        }
    }

    unsigned char sym = static_cast<unsigned char>(text[symbol_pos - 1]);
    if (sym == 'x' || sym == 'X')
    {
        int value = parseDigitsValue();
        if (value > 0)
        {
            begin_out = symbol_pos - 1;
            count_out = value;
            return true;
        }
    }

    return false;
}

struct RewardEntry
{
    std::string name;
    bool has_bullet = false;
    int count = 0;
};

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void trimUtf8InPlace(std::string& text)
{
    auto isAsciiSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };

    while (!text.empty())
    {
        if (isAsciiSpace(static_cast<unsigned char>(text.front())))
        {
            text.erase(text.begin());
            continue;
        }
        if (startsWith(text, kFullWidthSpace))
        {
            text.erase(0, kFullWidthSpace.size());
            continue;
        }
        break;
    }

    while (!text.empty())
    {
        if (isAsciiSpace(static_cast<unsigned char>(text.back())))
        {
            text.pop_back();
            continue;
        }
        if (text.size() >= kFullWidthSpace.size() &&
            text.compare(text.size() - kFullWidthSpace.size(), kFullWidthSpace.size(), kFullWidthSpace) == 0)
        {
            text.erase(text.size() - kFullWidthSpace.size());
            continue;
        }
        break;
    }
}

int extractCountAndTrim(std::string& text)
{
    trimUtf8InPlace(text);
    size_t begin = 0;
    int count = 0;
    if (tryParseTrailingCountSuffix(text, text.size(), begin, count))
    {
        text.erase(begin, text.size() - begin);
        trimUtf8InPlace(text);
        return count;
    }
    return 0;
}

std::vector<RewardEntry> parseRewardEntries(const std::string& text)
{
    std::vector<RewardEntry> entries;
    if (text.empty())
        return entries;

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        std::string working = line;
        trimUtf8InPlace(working);
        if (working.empty())
            continue;

        RewardEntry entry;
        if (startsWith(working, kBullet))
        {
            entry.has_bullet = true;
            working.erase(0, kBullet.size());
            trimUtf8InPlace(working);
        }

        entry.count = extractCountAndTrim(working);
        trimUtf8InPlace(working);

        if (working.empty())
            continue;

        entry.name = working;
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }
    if (!text.empty() && text.back() == '\n')
    {
        lines.emplace_back();
    }
    return lines;
}

void removeTrailingQuantity(std::string& text)
{
    bool modified = false;
    do
    {
        modified = false;
        trimUtf8InPlace(text);

        size_t begin = 0;
        int count = 0;
        if (tryParseTrailingCountSuffix(text, text.size(), begin, count))
        {
            text.erase(begin, text.size() - begin);
            trimUtf8InPlace(text);
            modified = true;
            continue;
        }

        if (tryParseTrailingMultiplier(text, text.size(), begin, count))
        {
            text.erase(begin, text.size() - begin);
            trimUtf8InPlace(text);
            modified = true;
        }
    }
    while (modified);
}

std::string buildRewardDisplay(const std::vector<RewardEntry>& base_entries, const std::string& translated_text)
{
    if (base_entries.empty())
    {
        std::string fallback = translated_text;
        trimUtf8InPlace(fallback);
        return fallback;
    }

    auto translated_lines = splitLines(translated_text);
    std::string result;

    for (size_t i = 0; i < base_entries.size(); ++i)
    {
        const RewardEntry& base = base_entries[i];
        std::string line = (i < translated_lines.size()) ? translated_lines[i] : base.name;
        trimUtf8InPlace(line);

        if (base.has_bullet && startsWith(line, kBullet))
        {
            line.erase(0, kBullet.size());
            trimUtf8InPlace(line);
        }

        removeTrailingQuantity(line);

        if (line.empty())
            line = base.name;

        std::string composed;
        if (base.has_bullet)
        {
            composed += kBullet;
            if (!line.empty())
                composed += ' ';
        }

        composed += line;

        if (base.count > 1)
        {
            if (!line.empty())
                composed += ' ';
            composed += kMultiplierSymbol;
            composed += std::to_string(base.count);
        }

        trimUtf8InPlace(composed);
        if (!composed.empty())
        {
            if (!result.empty())
                result += '\n';
            result += composed;
        }
    }

    for (size_t i = base_entries.size(); i < translated_lines.size(); ++i)
    {
        std::string extra = translated_lines[i];
        trimUtf8InPlace(extra);
        if (extra.empty())
            continue;
        if (!result.empty())
            result += '\n';
        result += extra;
    }

    return result;
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

QuestWindow::QuestWindow(FontManager& font_manager, const std::string& name, bool is_default)
    : font_manager_(font_manager)
    , name_(name)
{
    static int quest_counter = 0;
    ++quest_counter;
    id_suffix_ = "Quest" + std::to_string(quest_counter);
    settings_id_suffix_ = id_suffix_ + "_settings";
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
    is_default_instance_ = is_default;

    state_.applyDefaults();
    state_.ui_state().width = 580.0f;
    state_.ui_state().height = 420.0f;
    state_.ui_state().window_size = ImVec2(state_.ui_state().width, state_.ui_state().height);
    state_.ui_state().pending_resize = true;
    state_.ui_state().pending_reposition = true;

    session_.setCapacity(5000);
    session_.enableCache(true);

    settings_view_ = std::make_unique<QuestSettingsView>(state_, font_manager_, session_);

    font_manager_.registerDialog(state_.ui_state());
    refreshFontBinding();
}

QuestWindow::~QuestWindow()
{
    font_manager_.unregisterDialog(state_.ui_state());
    if (translator_) {
        translator_->shutdown();
        translator_.reset();
    }
}

void QuestWindow::rename(const char* new_name)
{
    if (!new_name || !new_name[0]) return;
    name_ = new_name;
    window_label_ = name_ + "###" + id_suffix_;
    settings_window_label_ = name_ + " Settings###" + settings_id_suffix_;
}

void QuestWindow::refreshFontBinding()
{
    font_manager_.ensureFont(state_.ui_state());
}

void QuestWindow::applyQuestUpdate()
{
    auto* launcher = DQXClarityService_Get();
    if (!launcher) return;

    dqxclarity::QuestMessage msg;
    if (!launcher->getLatestQuest(msg)) return;
    if (msg.seq == 0 || msg.seq == last_applied_seq_) return;

    state_.quest.subquest_name = msg.subquest_name;
    state_.quest.quest_name = msg.quest_name;
    state_.quest.description = msg.description;
    state_.quest.rewards = msg.rewards;
    state_.quest.repeat_rewards = msg.repeat_rewards;
    state_.quest.seq = msg.seq;
    last_applied_seq_ = msg.seq;

    state_.original.subquest_name = msg.subquest_name;
    state_.original.quest_name = msg.quest_name;
    state_.original.description = msg.description;
    state_.original.rewards = msg.rewards;
    state_.original.repeat_rewards = msg.repeat_rewards;

    resetTranslationState();
    appended_since_last_frame_ = true;

    const TranslationConfig& config = activeTranslationConfig();
    if (config.translate_enabled && !state_.quest.description.empty()) {
        submitTranslationRequest();
    }
}

void QuestWindow::resetTranslationState()
{
    for (auto& status : field_status_) {
        status = FieldStatus{};
    }
    job_lookup_.clear();
    state_.translated.applyDefaults();
    state_.translation_valid = false;
    state_.translation_failed = false;
    state_.translation_error.clear();
}

void QuestWindow::render()
{
    appended_since_last_frame_ = false;

    applyQuestUpdate();
    processTranslatorEvents();
    refreshFontBinding();

    bool requeue_translation = false;
    bool using_global = usingGlobalTranslation();
    if (using_global)
    {
        if (auto* cm = ConfigManager_Get())
        {
            std::uint64_t version = cm->globalTranslationVersion();
            if (version != observed_global_translation_version_)
            {
                observed_global_translation_version_ = version;
                resetTranslatorState();
                requeue_translation = true;
            }
        }
    }
    else
    {
        if (last_used_global_translation_)
        {
            resetTranslatorState();
            requeue_translation = true;
        }
        observed_global_translation_version_ = 0;
    }
    last_used_global_translation_ = using_global;

    const TranslationConfig& config = activeTranslationConfig();

    if (requeue_translation && config.translate_enabled)
    {
        submitTranslationRequest();
    }

    ImGuiIO& io = ImGui::GetIO();

    const float max_width = std::max(380.0f, io.DisplaySize.x - 40.0f);
    const float max_height = std::max(320.0f, io.DisplaySize.y - 40.0f);

    state_.ui_state().width = std::clamp(state_.ui_state().width, 380.0f, max_width);
    state_.ui_state().height = std::clamp(state_.ui_state().height, 320.0f, max_height);
    state_.ui_state().padding.x = std::clamp(state_.ui_state().padding.x, 4.0f, 80.0f);
    state_.ui_state().padding.y = std::clamp(state_.ui_state().padding.y, 4.0f, 80.0f);
    state_.ui_state().rounding = std::clamp(state_.ui_state().rounding, 0.0f, 32.0f);
    state_.ui_state().border_thickness = std::clamp(state_.ui_state().border_thickness, 0.5f, 6.0f);

    bool fade_enabled = state_.ui_state().fade_enabled;

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
                }
            }
        }
    }

    if (state_.ui_state().pending_reposition) {
        const ImVec2 anchor(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    } else {
        ImGui::SetNextWindowPos(state_.ui_state().window_pos, ImGuiCond_FirstUseEver);
    }

    if (state_.ui_state().pending_resize) {
        ImGui::SetNextWindowSize(ImVec2(state_.ui_state().width, state_.ui_state().height), ImGuiCond_Always);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 320.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (auto* cm = ConfigManager_Get()) {
        if (DockState::IsScattering()) {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
        } else if (cm->getAppMode() == ConfigManager::AppMode::Mini) {
            ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
        }
    }

    const float fade_alpha = state_.ui_state().current_alpha_multiplier;
    float effective_alpha = state_.ui_state().background_alpha * fade_alpha;
    UITheme::pushDialogStyle(effective_alpha,
                             state_.ui_state().padding,
                             state_.ui_state().rounding,
                             state_.ui_state().border_thickness,
                             state_.ui_state().border_enabled);
    const float style_alpha = std::max(fade_alpha, 0.001f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style_alpha);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (auto* cm = ConfigManager_Get()) {
        if (cm->getAppMode() == ConfigManager::AppMode::Mini) {
            flags |= ImGuiWindowFlags_NoMove;
        }
    }

    if (ImGui::Begin(window_label_.c_str(), nullptr, flags)) {
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();

        state_.ui_state().width = win_size.x;
        state_.ui_state().height = win_size.y;

        bool is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (!is_hovered && fade_enabled && fade_alpha < 0.99f)
        {
            ImVec2 window_max(win_pos.x + win_size.x, win_pos.y + win_size.y);
            is_hovered = ImGui::IsMouseHoveringRect(win_pos, window_max, false);
        }
        

        ui::RenderVignette(win_pos,
                            win_size,
                            state_.ui_state().vignette_thickness,
                            state_.ui_state().rounding,
                            state_.ui_state().current_alpha_multiplier);

        ImFont* active_font = state_.ui_state().font;
        float font_scale = 1.0f;
        if (active_font && state_.ui_state().font_base_size > 0.0f) {
            font_scale = std::max(0.3f, state_.ui_state().font_size / state_.ui_state().font_base_size);
        }
        if (active_font) {
            ImGui::PushFont(active_font);
            ImGui::SetWindowFontScale(font_scale);
        }

        const float wrap_width = std::max(60.0f, state_.ui_state().width - state_.ui_state().padding.x * 2.0f);
        renderQuestContent(wrap_width);

        if (active_font) {
            ImGui::PopFont();
            ImGui::SetWindowFontScale(1.0f);
        }

        // Unified animator update (fade + optional scroll)
        animator_.update(state_.ui_state(), io.DeltaTime, appended_since_last_frame_, is_hovered);

        state_.ui_state().window_pos = win_pos;
        state_.ui_state().window_size = win_size;
        state_.ui_state().pending_reposition = false;
        state_.ui_state().pending_resize = false;
        state_.ui_state().is_docked = ImGui::IsWindowDocked();
    }
    ImGui::End();

    ImGui::PopStyleVar();
    UITheme::popDialogStyle();

    renderContextMenu();
    renderSettingsWindow();
}

void QuestWindow::renderSettingsWindow()
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
        settings_view_->render(translator_.get(),
                               apply_hint_,
                               apply_hint_timer_,
                               testing_connection_,
                               test_result_,
                               test_timestamp_,
                               settings_id_suffix_,
                               [this]() { this->initTranslatorIfEnabled(); },
                               [this]() -> translate::ITranslator* { return translator_.get(); });
    }
    ImGui::End();
}

void QuestWindow::renderSettings()
{
    show_settings_window_ = true;
}

void QuestWindow::renderQuestContent(float wrap_width)
{
    const std::string title_text = displayStringFor(QuestField::Title);
    const std::string quest_label = title_text.empty()
        ? ui::LocalizedOrFallback("quest.title.empty", "(No Quest)")
        : title_text;

    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);

    const std::string subquest_text = displayStringFor(QuestField::SubQuest);
    if (!subquest_text.empty()) {
        ImVec2 sub_size = ImGui::CalcTextSize(subquest_text.c_str(), nullptr, false, wrap_width);
        float start_x = ImGui::GetCursorPosX() + std::max(0.0f, (wrap_width - sub_size.x) * 0.5f);
        ImVec2 original_pos = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(start_x);
        ImVec2 sub_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(subquest_text.c_str(), sub_pos, ImGui::GetFont(), ImGui::GetFontSize(), wrap_width);
        ImGui::Dummy(ImVec2(0.0f, sub_size.y));
        ImGui::SetCursorPosX(original_pos.x);
        ImGui::Spacing();
    }

    ImVec2 title_size = ImGui::CalcTextSize(quest_label.c_str(), nullptr, false, wrap_width);
    ImVec2 title_pos = ImGui::GetCursorScreenPos();
    title_pos.x = ImGui::GetWindowPos().x + state_.ui_state().padding.x + (wrap_width - title_size.x) * 0.5f;
    title_pos.y = ImGui::GetCursorScreenPos().y;
    const float base_font_size = ImGui::GetFontSize();
    const float title_font_scale = 1.25f;
    const float title_font_size = base_font_size * title_font_scale;
    ui::RenderOutlinedText(quest_label.c_str(), title_pos, ImGui::GetFont(), title_font_size, wrap_width);
    const float title_height = ImGui::GetTextLineHeightWithSpacing() * title_font_scale;
    ImGui::Dummy(ImVec2(0.0f, title_height));

    ui::DrawDefaultSeparator();

    const float horizontal_padding = state_.ui_state().padding.x;
    const float vertical_padding = state_.ui_state().padding.y;
    float desc_top_margin = std::max(horizontal_padding + 10.0f, vertical_padding + 6.0f);
    desc_top_margin = std::max(desc_top_margin, 18.0f);
    float desc_bottom_margin = std::max(12.0f, vertical_padding * 0.4f);
    const float description_side_margin = std::max(16.0f, horizontal_padding * 0.5f);
    const float description_wrap_width = std::max(20.0f, wrap_width - description_side_margin * 2.0f);

    ImGui::Dummy(ImVec2(0.0f, desc_top_margin));

    const std::string description_text = displayStringFor(QuestField::Description);
    if (!description_text.empty()) {
        ImVec2 desc_pos = ImGui::GetCursorScreenPos();
        desc_pos.x += description_side_margin;
        ui::RenderOutlinedText(description_text.c_str(), desc_pos, ImGui::GetFont(), ImGui::GetFontSize(), description_wrap_width);
        ImVec2 desc_size = ImGui::CalcTextSize(description_text.c_str(), nullptr, false, description_wrap_width);
        ImGui::Dummy(ImVec2(0.0f, desc_size.y));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const std::string fallback = ui::LocalizedOrFallback("quest.description.empty", "No quest description available");
        ImVec2 desc_pos = ImGui::GetCursorScreenPos();
        desc_pos.x += description_side_margin;
        ui::RenderOutlinedText(fallback.c_str(), desc_pos, ImGui::GetFont(), ImGui::GetFontSize(), description_wrap_width);
        ImVec2 desc_size = ImGui::CalcTextSize(fallback.c_str(), nullptr, false, description_wrap_width);
        ImGui::Dummy(ImVec2(0.0f, desc_size.y));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, desc_bottom_margin));

    ImGui::Spacing();
    renderTranslationControls(wrap_width);

    float grid_height = estimateGridHeight(wrap_width);
    float available = ImGui::GetContentRegionAvail().y;
    if (available > grid_height)
    {
        ImGui::Dummy(ImVec2(0.0f, available - grid_height));
    }

    renderRewardsRow(wrap_width);

    ImGui::PopTextWrapPos();
}

void QuestWindow::renderRewardsRow(float wrap_width)
{
    const std::string rewards_label = ui::LocalizedOrFallback("quest.rewards.label", "Rewards");
    const std::string repeat_label = ui::LocalizedOrFallback("quest.repeat_rewards.label", "Repeat Rewards");

    std::string rewards_text = displayStringFor(QuestField::Rewards);
    std::string repeat_text = displayStringFor(QuestField::RepeatRewards);

    if (rewards_text.empty()) {
        rewards_text = ui::LocalizedOrFallback("quest.rewards.empty", "None");
    }
    if (repeat_text.empty()) {
        repeat_text = ui::LocalizedOrFallback("quest.repeat_rewards.empty", "None");
    }

    const auto base_rewards_entries = parseRewardEntries(state_.quest.rewards);
    const auto base_repeat_entries = parseRewardEntries(state_.quest.repeat_rewards);

    const std::string formatted_rewards = buildRewardDisplay(base_rewards_entries, rewards_text);
    const std::string formatted_repeat = buildRewardDisplay(base_repeat_entries, repeat_text);

    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 table_size(wrap_width, 0.0f);
    const float cell_padding_y = style.CellPadding.y * 0.9f;
    float divider_y = 0.0f;
    float column_width = wrap_width * 0.5f;
    float column_width_actual = column_width;

    ImVec2 table_min{}, table_max{};

    if (ImGui::BeginTable("QuestRewardGrid", 2, ImGuiTableFlags_SizingStretchSame, table_size)) {
        ImGui::TableSetupColumn("RewardsCol", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("RepeatRewardsCol", ImGuiTableColumnFlags_WidthStretch);

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, cell_padding_y));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        float rewards_header_wrap = std::max(1.0f, ImGui::GetColumnWidth() - style.CellPadding.x * 2.0f);
        ImVec2 rewards_header_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(rewards_label.c_str(), rewards_header_pos, ImGui::GetFont(), ImGui::GetFontSize(), rewards_header_wrap);
        ImVec2 rewards_header_size = ImGui::CalcTextSize(rewards_label.c_str(), nullptr, false, rewards_header_wrap);
        ImGui::Dummy(ImVec2(0.0f, rewards_header_size.y));

        ImGui::TableNextColumn();
        float repeat_header_wrap = std::max(1.0f, ImGui::GetColumnWidth() - style.CellPadding.x * 2.0f);
        ImVec2 repeat_header_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(repeat_label.c_str(), repeat_header_pos, ImGui::GetFont(), ImGui::GetFontSize(), repeat_header_wrap);
        ImVec2 repeat_header_size = ImGui::CalcTextSize(repeat_label.c_str(), nullptr, false, repeat_header_wrap);
        ImGui::Dummy(ImVec2(0.0f, repeat_header_size.y));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        divider_y = ImGui::GetCursorScreenPos().y;
        float wrap_limit_left = std::max(1.0f, ImGui::GetColumnWidth() - style.CellPadding.x * 2.0f);
        ImVec2 rewards_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(formatted_rewards.c_str(), rewards_pos, ImGui::GetFont(), ImGui::GetFontSize(), wrap_limit_left);
        ImVec2 rewards_size = ImGui::CalcTextSize(formatted_rewards.c_str(), nullptr, false, wrap_limit_left);
        ImGui::Dummy(ImVec2(0.0f, rewards_size.y));

        ImGui::TableNextColumn();
        float wrap_limit_right = std::max(1.0f, ImGui::GetColumnWidth() - style.CellPadding.x * 2.0f);
        ImVec2 repeat_pos = ImGui::GetCursorScreenPos();
        ui::RenderOutlinedText(formatted_repeat.c_str(), repeat_pos, ImGui::GetFont(), ImGui::GetFontSize(), wrap_limit_right);
        ImVec2 repeat_size = ImGui::CalcTextSize(formatted_repeat.c_str(), nullptr, false, wrap_limit_right);
        ImGui::Dummy(ImVec2(0.0f, repeat_size.y));

        ImGui::PopStyleVar();
        ImGui::EndTable();

        table_min = ImGui::GetItemRectMin();
        table_max = ImGui::GetItemRectMax();
        column_width_actual = (table_max.x - table_min.x) * 0.5f;
    }

    if (table_max.y > table_min.y) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float thickness = std::max(1.0f, state_.ui_state().border_thickness);
        ImVec4 color_v = UITheme::dialogSeparatorColor();
        color_v.w *= style.Alpha;
        ImU32 color = ImGui::ColorConvertFloat4ToU32(color_v);

        float top_y = table_min.y;
        float split_y = std::clamp(divider_y, table_min.y, table_max.y);
        float bottom_y = table_max.y;
        float split_x = table_min.x + column_width_actual;

        dl->AddLine(ImVec2(table_min.x, top_y), ImVec2(table_max.x, top_y), color, thickness);
        dl->AddLine(ImVec2(table_min.x, split_y), ImVec2(table_max.x, split_y), color, thickness);
        dl->AddLine(ImVec2(split_x, top_y), ImVec2(split_x, bottom_y), color, thickness);
    }
}

void QuestWindow::renderTranslationControls(float wrap_width)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);

    const TranslationConfig& config = activeTranslationConfig();

    if (!config.translate_enabled) {
        ImGui::TextDisabled("%s", ui::LocalizedOrFallback("quest.translation.disabled", "Translation disabled. Enable it in settings.").c_str());
        ImGui::PopTextWrapPos();
        return;
    }

    bool any_pending = !job_lookup_.empty();
    if (any_pending) {
        ImGui::TextDisabled("%s", ui::LocalizedOrFallback("quest.translation.pending", "Translating...").c_str());
    }

    if (state_.translation_failed && !state_.translation_error.empty()) {
        ImGui::TextColored(UITheme::errorColor(), "%s", state_.translation_error.c_str());
    }

    ImGui::PopTextWrapPos();

    const bool has_failure = std::any_of(field_status_.begin(), field_status_.end(), [](const FieldStatus& s) { return s.failed; });
    if (has_failure) {
        if (ImGui::Button(ui::LocalizedOrFallback("dialog.translate.timeout.copy", "Copy").c_str())) {
            std::string buffer = buildCopyBuffer();
            if (!buffer.empty()) {
                ImGui::SetClipboardText(buffer.c_str());
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(ui::LocalizedOrFallback("dialog.translate.timeout.retry", "Retry").c_str())) {
            submitTranslationRequest();
        }
    }
}

void QuestWindow::renderContextMenu()
{
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    bool within_window = ImGui::IsMousePosValid(&mouse_pos) &&
        ImGui::IsMouseHoveringRect(state_.ui_state().window_pos,
            ImVec2(state_.ui_state().window_pos.x + state_.ui_state().window_size.x,
                   state_.ui_state().window_pos.y + state_.ui_state().window_size.y), false);

    std::string popup_id = "QuestContextMenu###" + id_suffix_;

    if (within_window && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup(popup_id.c_str());
    }

    bool is_docked = state_.ui_state().is_docked;
    int quest_count = 0;
    if (auto* cm = ConfigManager_Get())
    {
        if (auto* reg = cm->registry())
        {
            quest_count = static_cast<int>(reg->windowsByType(UIWindowType::Quest).size());
        }
    }

    if (ImGui::BeginPopup(popup_id.c_str()))
    {
        if (ImGui::MenuItem(i18n::get("common.settings")))
        {
            show_settings_window_ = !show_settings_window_;
        }

        ImGui::Separator();

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

        bool can_remove = quest_count > 1;
        if (ImGui::MenuItem(i18n::get("common.remove"), nullptr, false, can_remove))
        {
            should_be_removed_ = true;
        }

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

void QuestWindow::initTranslatorIfEnabled()
{
    const TranslationConfig& config = activeTranslationConfig();
    if (!config.translate_enabled) {
        resetTranslatorState();
        translator_error_reported_ = false;
        return;
    }

    translate::BackendConfig cfg = translate::BackendConfig::from(config);
    std::string incomplete_reason;
    if (translatorConfigIncomplete(cfg, incomplete_reason))
    {
        if (!translator_error_reported_)
        {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Translation,
                                              utils::ErrorSeverity::Info,
                                              "Quest translator disabled: configuration incomplete",
                                              incomplete_reason);
            translator_error_reported_ = true;
        }
        resetTranslatorState();
        cached_config_ = translate::BackendConfig{};
        translator_error_reported_ = true;
        return;
    }

    bool same_backend = translator_initialized_ && translator_ && cfg.backend == cached_backend_;
    bool same_config = same_backend &&
                       cfg.base_url == cached_config_.base_url &&
                       cfg.model == cached_config_.model &&
                       cfg.api_key == cached_config_.api_key &&
                       cfg.api_secret == cached_config_.api_secret &&
                       cfg.target_lang == cached_config_.target_lang;

    if (same_config && translator_ && translator_->isReady()) {
        translator_error_reported_ = false;
        return;
    }

    if (translator_) {
        translator_->shutdown();
        translator_.reset();
    }

    translator_ = translate::createTranslator(cfg.backend);
    if (!translator_ || !translator_->init(cfg)) {
        std::string details;
        if (translator_) {
            PLOG_WARNING << "Quest translator init failed for backend " << static_cast<int>(cfg.backend);
            if (const char* last = translator_->lastError())
                details = last;
            translator_->shutdown();
            translator_.reset();
        } else {
            PLOG_WARNING << "Quest translator creation failed for backend " << static_cast<int>(cfg.backend);
        }
        if (!translator_error_reported_) {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Translation,
                                              utils::ErrorSeverity::Warning,
                                              "Quest translator failed to initialize",
                                              details.empty() ? (std::string("Backend index: ") + std::to_string(static_cast<int>(cfg.backend))) : details);
            translator_error_reported_ = true;
        }
        resetTranslatorState();
        return;
    }

    if (!translator_->isReady()) {
        std::string details;
        if (!translator_error_reported_) {
            if (const char* last = translator_->lastError()) {
                details = last;
            }
        }
        PLOG_WARNING << "Quest translator not ready after init for backend " << static_cast<int>(cfg.backend);
        resetTranslatorState();
        if (!translator_error_reported_) {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Translation,
                                              utils::ErrorSeverity::Warning,
                                              "Quest translator backend is not ready",
                                              details.empty() ? (std::string("Backend index: ") + std::to_string(static_cast<int>(cfg.backend))) : details);
            translator_error_reported_ = true;
        }
    } else {
        translator_initialized_ = true;
        cached_backend_ = cfg.backend;
        cached_config_ = cfg;
        translator_error_reported_ = false;
    }
}

void QuestWindow::processTranslatorEvents()
{
    if (!translator_) return;

    std::vector<translate::Completed> done;
    if (!translator_->drain(done) || done.empty()) return;

    std::vector<TranslateSession::CompletedEvent> events;
    session_.onCompleted(done, events);

    for (const auto& ev : events) {
        auto it = job_lookup_.find(ev.job_id);
        if (it == job_lookup_.end()) {
            continue;
        }
        QuestField field = it->second;
        job_lookup_.erase(it);

        if (ev.failed) {
            handleTranslationFailure(field, ev.error_message.empty()
                ? ui::LocalizedOrFallback("quest.translation.error", "Translation failed")
                : ev.error_message);
        } else {
            applyCachedTranslation(field, ev.text);
        }
    }

    refreshTranslationFlags();
}

void QuestWindow::submitTranslationRequest()
{
    initTranslatorIfEnabled();
    if (!translator_ || !translator_->isReady()) {
        state_.translation_failed = true;
        state_.translation_error = ui::LocalizedOrFallback("quest.translation.not_ready", "Translator not ready.");
        appended_since_last_frame_ = true;
        refreshTranslationFlags();
        return;
    }

    const TranslationConfig& config = activeTranslationConfig();
    job_lookup_.clear();
    for (auto& status : field_status_) {
        status = FieldStatus{};
    }
    state_.translated.applyDefaults();
    state_.translation_error.clear();
    state_.translation_failed = false;

    submitFieldTranslation(QuestField::SubQuest, state_.quest.subquest_name, config);
    submitFieldTranslation(QuestField::Title, state_.quest.quest_name, config);
    submitFieldTranslation(QuestField::Description, state_.quest.description, config);
    submitFieldTranslation(QuestField::Rewards, state_.quest.rewards, config);
    submitFieldTranslation(QuestField::RepeatRewards, state_.quest.repeat_rewards, config);

    refreshTranslationFlags();
}

void QuestWindow::submitFieldTranslation(QuestField field, const std::string& text, const TranslationConfig& config)
{
    FieldStatus& status = fieldStatus(field);
    status = FieldStatus{};

    if (text.empty()) {
        status.has_translation = true;
        status.failed = false;
        return;
    }

    auto submit = session_.submit(text,
                                  config.translation_backend,
                                  config.target_lang_enum,
                                  translator_.get());

    if (submit.kind == TranslateSession::SubmitKind::Cached) {
        applyCachedTranslation(field, submit.text);
        return;
    }

    if (submit.kind == TranslateSession::SubmitKind::Queued && submit.job_id != 0) {
        status.job_id = submit.job_id;
        job_lookup_[submit.job_id] = field;
        return;
    }

    handleTranslationFailure(field, ui::LocalizedOrFallback("quest.translation.queue_failed", "Unable to queue translation request."));
}

void QuestWindow::applyCachedTranslation(QuestField field, const std::string& text)
{
    FieldStatus& status = fieldStatus(field);
    status.has_translation = true;
    status.failed = false;
    status.error.clear();
    status.job_id = 0;
    status.text = text;

    switch (field) {
    case QuestField::SubQuest: state_.translated.subquest_name = text; break;
    case QuestField::Title: state_.translated.quest_name = text; break;
    case QuestField::Description: state_.translated.description = text; break;
    case QuestField::Rewards: state_.translated.rewards = text; break;
    case QuestField::RepeatRewards: state_.translated.repeat_rewards = text; break;
    }
    appended_since_last_frame_ = true;
}

void QuestWindow::handleTranslationFailure(QuestField field, const std::string& message)
{
    FieldStatus& status = fieldStatus(field);
    status.failed = true;
    status.has_translation = false;
    status.error = message;
    status.job_id = 0;
    state_.translation_failed = true;
    if (state_.translation_error.empty()) {
        state_.translation_error = message;
    }
    appended_since_last_frame_ = true;
}

void QuestWindow::refreshTranslationFlags()
{
    bool any_failure = false;
    bool all_resolved = true;

    auto consider_field = [&](QuestField field, const std::string& source) {
        const FieldStatus& status = fieldStatus(field);
        if (status.failed) {
            any_failure = true;
            all_resolved = false;
            if (state_.translation_error.empty()) {
                state_.translation_error = status.error;
            }
        } else if (!status.has_translation && !source.empty()) {
            all_resolved = false;
        }
    };

    consider_field(QuestField::SubQuest, state_.quest.subquest_name);
    consider_field(QuestField::Title, state_.quest.quest_name);
    consider_field(QuestField::Description, state_.quest.description);
    consider_field(QuestField::Rewards, state_.quest.rewards);
    consider_field(QuestField::RepeatRewards, state_.quest.repeat_rewards);

    state_.translation_failed = any_failure;
    state_.translation_valid = all_resolved && job_lookup_.empty();
    if (!any_failure && job_lookup_.empty()) {
        state_.translation_error.clear();
    }
}

std::string QuestWindow::displayStringFor(QuestField field) const
{
    const FieldStatus& status = fieldStatus(field);
    if (status.has_translation) {
        switch (field) {
        case QuestField::SubQuest: return state_.translated.subquest_name;
        case QuestField::Title: return state_.translated.quest_name;
        case QuestField::Description: return state_.translated.description;
        case QuestField::Rewards: return state_.translated.rewards;
        case QuestField::RepeatRewards: return state_.translated.repeat_rewards;
        }
    }

    switch (field) {
    case QuestField::SubQuest: return state_.quest.subquest_name;
    case QuestField::Title: return state_.quest.quest_name;
    case QuestField::Description: return state_.quest.description;
    case QuestField::Rewards: return state_.quest.rewards;
    case QuestField::RepeatRewards: return state_.quest.repeat_rewards;
    }
    return {};
}

QuestWindow::FieldStatus& QuestWindow::fieldStatus(QuestField field)
{
    return field_status_[fieldIndex(field)];
}

const QuestWindow::FieldStatus& QuestWindow::fieldStatus(QuestField field) const
{
    return field_status_[fieldIndex(field)];
}

std::size_t QuestWindow::fieldIndex(QuestField field)
{
    return static_cast<std::size_t>(field);
}

std::string QuestWindow::buildCopyBuffer() const
{
    std::ostringstream oss;
    const std::string subquest = displayStringFor(QuestField::SubQuest);
    const std::string title = displayStringFor(QuestField::Title);
    const std::string description = displayStringFor(QuestField::Description);
    std::string rewards = displayStringFor(QuestField::Rewards);
    std::string repeat = displayStringFor(QuestField::RepeatRewards);

    if (!subquest.empty()) {
        oss << subquest << '\n';
    }
    if (!title.empty()) {
        oss << title << '\n';
    }
    if (!description.empty()) {
        oss << description << '\n';
    }
    if (!rewards.empty()) {
        std::string formatted_rewards = buildRewardDisplay(parseRewardEntries(state_.quest.rewards), rewards);
        if (formatted_rewards.empty()) {
            formatted_rewards = rewards;
        }
        oss << ui::LocalizedOrFallback("quest.rewards.label", "Rewards") << ": " << formatted_rewards << '\n';
    }
    if (!repeat.empty()) {
        std::string formatted_repeat = buildRewardDisplay(parseRewardEntries(state_.quest.repeat_rewards), repeat);
        if (formatted_repeat.empty()) {
            formatted_repeat = repeat;
        }
        oss << ui::LocalizedOrFallback("quest.repeat_rewards.label", "Repeat Rewards") << ": " << formatted_repeat;
    }

    return oss.str();
}

float QuestWindow::estimateGridHeight(float wrap_width) const
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float column_width = std::max(1.0f, wrap_width * 0.5f);
    float wrapping = column_width - style.CellPadding.x * 2.0f;
    if (wrapping <= 0.0f) wrapping = column_width;

    const auto base_rewards_entries = parseRewardEntries(state_.quest.rewards);
    const auto base_repeat_entries = parseRewardEntries(state_.quest.repeat_rewards);

    std::string rewards_display = displayStringFor(QuestField::Rewards);
    if (rewards_display.empty()) {
        rewards_display = ui::LocalizedOrFallback("quest.rewards.empty", "None");
    }
    std::string repeat_display = displayStringFor(QuestField::RepeatRewards);
    if (repeat_display.empty()) {
        repeat_display = ui::LocalizedOrFallback("quest.repeat_rewards.empty", "None");
    }

    std::string formatted_rewards = buildRewardDisplay(base_rewards_entries, rewards_display);
    if (formatted_rewards.empty()) {
        formatted_rewards = rewards_display;
    }
    std::string formatted_repeat = buildRewardDisplay(base_repeat_entries, repeat_display);
    if (formatted_repeat.empty()) {
        formatted_repeat = repeat_display;
    }

    ImVec2 rewards_size = ImGui::CalcTextSize(formatted_rewards.c_str(), nullptr, false, wrapping);
    ImVec2 repeat_size = ImGui::CalcTextSize(formatted_repeat.c_str(), nullptr, false, wrapping);

    float header_height = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    float body_height = std::max(rewards_size.y, repeat_size.y) + style.CellPadding.y * 2.0f;

    return header_height + body_height + style.ItemSpacing.y;
}

const TranslationConfig& QuestWindow::activeTranslationConfig() const
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

bool QuestWindow::usingGlobalTranslation() const
{
    return state_.use_global_translation && ConfigManager_Get() != nullptr;
}

void QuestWindow::resetTranslatorState()
{
    if (translator_)
    {
        translator_->shutdown();
        translator_.reset();
    }
    translator_initialized_ = false;
    translator_error_reported_ = false;
    cached_backend_ = translate::Backend::OpenAI;
    cached_config_ = {};
    job_lookup_.clear();
    for (auto& status : field_status_)
    {
        status = FieldStatus{};
    }
    state_.translation_failed = false;
    state_.translation_valid = false;
    state_.translation_error.clear();
}

