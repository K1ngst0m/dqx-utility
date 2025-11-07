#include "HelpWindow.hpp"
#include "../GlobalStateManager.hpp"
#include "../FontManager.hpp"
#include "../UITheme.hpp"
#include "../UIHelper.hpp"
#include "../DockState.hpp"
#include "../Localization.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../services/DQXClarityService.hpp"
#include "../../services/DQXClarityLauncher.hpp"
#include "../../dqxclarity/api/dqxclarity.hpp"
#include "../../platform/ProcessDetector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace
{

constexpr ImVec4 kOkColor{ 52.0f / 255.0f, 168.0f / 255.0f, 83.0f / 255.0f, 1.0f };
constexpr ImVec4 kWarningColor{ 211.0f / 255.0f, 168.0f / 255.0f, 0.0f, 1.0f };
constexpr ImVec4 kErrorColor{ 229.0f / 255.0f, 57.0f / 255.0f, 53.0f / 255.0f, 1.0f };
constexpr float kFadeDelaySeconds = 8.0f;
constexpr float kFadeDurationSeconds = 1.5f;

std::string trimWhitespace(std::string text)
{
    auto is_space = [](unsigned char c)
    {
        return std::isspace(c) != 0;
    };
    auto begin = std::find_if_not(text.begin(), text.end(),
                                  [&](char c)
                                  {
                                      return is_space(static_cast<unsigned char>(c));
                                  });
    auto end = std::find_if_not(text.rbegin(), text.rend(),
                                [&](char c)
                                {
                                    return is_space(static_cast<unsigned char>(c));
                                })
                   .base();
    if (begin >= end)
        return {};
    std::string result(begin, end);
    return result;
}

} // namespace

HelpWindow::HelpWindow(FontManager& font_manager, GlobalStateManager& global_state, ConfigManager& config, const std::string& name)
    : font_manager_(font_manager)
    , global_state_(global_state)
    , config_(config)
    , name_(name)
{
    static int counter = 0;
    id_suffix_ = "help_window_" + std::to_string(counter++);
    window_label_ = name_ + "###" + id_suffix_;

    state_.applyDefaults();
    state_.ui_state().width = 420.0f;
    state_.ui_state().height = 200.0f;
    state_.ui_state().fade_enabled = false;
    state_.ui_state().vignette_thickness = 0.0f;
    state_.ui_state().pending_resize = true;
    state_.ui_state().pending_reposition = true;
    state_.ui_state().current_alpha_multiplier = 1.0f;

    font_manager_.registerDialog(state_.ui_state());
    refreshFontBinding();
}

HelpWindow::~HelpWindow() { font_manager_.unregisterDialog(state_.ui_state()); }

void HelpWindow::rename(const char* new_name)
{
    if (!new_name)
        return;
    name_ = new_name;
    window_label_ = name_ + "###" + id_suffix_;
}

void HelpWindow::refreshFontBinding() { font_manager_.ensureFont(state_.ui_state()); }

ImVec4 HelpWindow::colorFor(StatusKind kind)
{
    switch (kind)
    {
    case StatusKind::Ok:
        return kOkColor;
    case StatusKind::Warning:
        return kWarningColor;
    case StatusKind::Error:
    default:
        return kErrorColor;
    }
}

std::string HelpWindow::sanitizeErrorMessage(const std::string& message)
{
    if (message.empty())
        return {};

    static constexpr std::string_view kToken{ "dqxclarity" };

    std::string buffer;
    buffer.reserve(message.size());

    for (std::size_t i = 0; i < message.size();)
    {
        bool match = false;
        if (i + kToken.size() <= message.size())
        {
            match = true;
            for (std::size_t j = 0; j < kToken.size(); ++j)
            {
                if (std::tolower(static_cast<unsigned char>(message[i + j])) != kToken[j])
                {
                    match = false;
                    break;
                }
            }
        }
        if (match)
        {
            i += kToken.size();
            continue;
        }

        buffer.push_back(message[i]);
        ++i;
    }

    std::string collapsed;
    collapsed.reserve(buffer.size());
    bool last_space = false;
    for (char c : buffer)
    {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc))
        {
            if (!last_space)
            {
                collapsed.push_back(' ');
                last_space = true;
            }
            continue;
        }
        if (c == ':' && (collapsed.empty() || collapsed.back() == ' '))
        {
            continue;
        }
        collapsed.push_back(c);
        last_space = false;
    }

    return trimWhitespace(collapsed);
}

HelpWindow::StatusInfo HelpWindow::evaluateStatus() const
{
    StatusInfo info{};

    auto* launcher = DQXClarityService_Get();
    bool dqx_running = false;
    if (launcher)
        dqx_running = launcher->isDQXGameRunning();
    else
        dqx_running = ProcessDetector::isProcessRunning("DQXGame.exe");

    if (!dqx_running)
    {
        info.kind = StatusKind::Warning;
        info.status_text = i18n::get("help.status.warning");
        info.message = ui::LocalizedOrFallback("help.message.game_not_running",
                                               "DQX is not running. Please launch the game first.");
        info.color = colorFor(info.kind);
        return info;
    }

    if (!launcher)
    {
        info.kind = StatusKind::Warning;
        info.status_text = i18n::get("help.status.warning");
        info.message = ui::LocalizedOrFallback("help.message.waiting_for_service",
                                               "Waiting for DQX-Utility to finish initializing...");
        info.color = colorFor(info.kind);
        return info;
    }

    using dqxclarity::Status;
    Status stage = launcher->getEngineStage();

    if (stage == Status::Error)
    {
        info.kind = StatusKind::Error;
        info.status_text = i18n::get("help.status.error");
        std::string raw = launcher->getLastErrorMessage();
        std::string sanitized = sanitizeErrorMessage(raw);
        std::string generic = ui::LocalizedOrFallback("help.message.error_generic",
                                                      "Initialization failed. Please check the logs for details.");
        if (sanitized.empty())
        {
            info.message = generic;
        }
        else
        {
            info.message = i18n::format("help.message.error_with_reason", {
                                                                              { "reason", sanitized }
            });
        }
        info.color = colorFor(info.kind);
        return info;
    }

    if (stage == Status::Hooked)
    {
        info.kind = StatusKind::Ok;
        info.status_text = i18n::get("help.status.ok");
        info.message = ui::LocalizedOrFallback("help.message.ready", "All systems are online.");
        info.color = colorFor(info.kind);
        return info;
    }

    info.kind = StatusKind::Warning;
    info.status_text = i18n::get("help.status.warning");
    info.message = ui::LocalizedOrFallback("help.message.waiting_for_service",
                                           "Waiting for DQX-Utility to finish initializing...");
    info.color = colorFor(info.kind);
    return info;
}

void HelpWindow::renderStatusMessage(const StatusInfo& info, ImFont* font, float wrap_width)
{
    const char* text_begin = info.message.c_str();
    const char* text_end = text_begin + info.message.size();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float font_size_px = ImGui::GetFontSize();
    ImVec2 text_extent = font->CalcTextSizeA(font_size_px, wrap_width, wrap_width, text_begin, text_end, nullptr);
    ui::RenderOutlinedText(text_begin, cursor, font, font_size_px, wrap_width);
    ImGui::Dummy(ImVec2(0.0f, text_extent.y + font_size_px * 0.25f));
}

void HelpWindow::render()
{
    refreshFontBinding();

    ImGuiIO& io = ImGui::GetIO();
    const float min_width = 900.0f;
    const float min_height = 400.0f;
    const float max_width = std::max(min_width, io.DisplaySize.x - 40.0f);
    const float max_height = std::max(min_height, io.DisplaySize.y - 40.0f);

    state_.ui_state().width = std::clamp(state_.ui_state().width, min_width, max_width);
    state_.ui_state().height = std::clamp(state_.ui_state().height, min_height, max_height);
    state_.ui_state().padding.x = std::clamp(state_.ui_state().padding.x, 4.0f, 80.0f);
    state_.ui_state().padding.y = std::clamp(state_.ui_state().padding.y, 4.0f, 80.0f);
    state_.ui_state().rounding = std::clamp(state_.ui_state().rounding, 0.0f, 32.0f);
    state_.ui_state().border_thickness = std::clamp(state_.ui_state().border_thickness, 0.5f, 6.0f);

    if (state_.ui_state().pending_reposition)
    {
        const ImVec2 anchor(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.3f);
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    else
    {
        ImGui::SetNextWindowPos(state_.ui_state().window_pos, ImGuiCond_FirstUseEver);
    }

    if (state_.ui_state().pending_resize)
    {
        ImGui::SetNextWindowSize(ImVec2(state_.ui_state().width, state_.ui_state().height), ImGuiCond_Always);
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(min_width, min_height), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    else if (global_state_.appMode() == GlobalStateManager::AppMode::Mini)
    {
        ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
    }

    StatusInfo status = evaluateStatus();
    updateFadeState(status.kind, last_hovered_, io.DeltaTime);
    if (status.kind == StatusKind::Ok)
    {
        int seconds = static_cast<int>(std::ceil(std::max(0.0f, countdown_seconds_)));
        status.message = i18n::format("help.message.ready_with_timer", {
                                                                           { "seconds", std::to_string(seconds) }
        });
    }

    UITheme::pushDialogStyle(state_.ui_state().background_alpha, state_.ui_state().padding, state_.ui_state().rounding,
                             state_.ui_state().border_thickness, state_.ui_state().border_enabled);
    ImGui::PushStyleColor(ImGuiCol_Border, status.color);
    ImGui::PushStyleColor(ImGuiCol_Text, status.color);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade_alpha_);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (global_state_.appMode() == GlobalStateManager::AppMode::Mini)
    {
        flags |= ImGuiWindowFlags_NoMove;
    }

    if (ImGui::Begin(window_label_.c_str(), nullptr, flags))
    {
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();

        state_.ui_state().window_pos = win_pos;
        state_.ui_state().window_size = win_size;
        state_.ui_state().pending_reposition = false;
        state_.ui_state().pending_resize = false;
        state_.ui_state().is_docked = ImGui::IsWindowDocked();

        bool hovered =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows);
        last_hovered_ = hovered;

        const float wrap_width = std::max(80.0f, state_.ui_state().width - state_.ui_state().padding.x * 2.0f);

        ImFont* active_font = state_.ui_state().font ? state_.ui_state().font : ImGui::GetFont();
        constexpr float kHelpFontScale = 1.25f;
        float font_scale = 1.0f;
        if (state_.ui_state().font && state_.ui_state().font_base_size > 0.0f)
        {
            font_scale = std::max(0.3f, state_.ui_state().font_size / state_.ui_state().font_base_size);
            ImGui::PushFont(state_.ui_state().font);
        }

        ImGui::SetWindowFontScale(font_scale * kHelpFontScale);
        renderStatusMessage(status, active_font, wrap_width);
        renderHelpTips(status.color, wrap_width);

        if (state_.ui_state().font)
        {
            ImGui::PopFont();
        }
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    UITheme::popDialogStyle();
}

void HelpWindow::renderSettings()
{
    // Help window currently has no per-instance settings.
}

void HelpWindow::renderHelpTips(const ImVec4& color, float wrap_width)
{
    ImGui::Spacing();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
    ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
    float x1 = win_pos.x + cr_min.x;
    float x2 = win_pos.x + cr_max.x;
    float thickness = 6.0f;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float alpha = color.w * ImGui::GetStyle().Alpha * fade_alpha_;
    ImVec4 adjusted = color;
    adjusted.w = alpha;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(adjusted);
    draw_list->AddRectFilled(ImVec2(x1, cursor.y), ImVec2(x2, cursor.y + thickness), col);
    ImGui::Dummy(ImVec2(0.0f, thickness));
    ImGui::Spacing();
    ImGui::TextUnformatted(i18n::get("help.tips.header"));
    ImGui::Spacing();

    auto render_tip = [&](const char* key)
    {
        const char* text = i18n::get(key);
        ImGui::Bullet();
        ImGui::SameLine();
        float start_x = ImGui::GetCursorPosX();
        ImGui::PushTextWrapPos(start_x + wrap_width);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
    };

    render_tip("help.tips.global_context");
    render_tip("help.tips.window_context");
    render_tip("help.tips.drag_hint");
}

void HelpWindow::updateFadeState(StatusKind kind, bool hovered, float delta_time)
{
    if (kind == StatusKind::Ok)
    {
        if (hovered)
        {
            ok_idle_timer_ = 0.0f;
            fade_alpha_ = 1.0f;
        }
        else
        {
            ok_idle_timer_ = std::min(ok_idle_timer_ + delta_time, kFadeDelaySeconds + kFadeDurationSeconds);
            if (ok_idle_timer_ < kFadeDelaySeconds)
            {
                fade_alpha_ = 1.0f;
            }
            else
            {
                float excess = ok_idle_timer_ - kFadeDelaySeconds;
                float progress = std::min(excess / kFadeDurationSeconds, 1.0f);
                fade_alpha_ = std::max(0.0f, 1.0f - progress);
            }
        }
        countdown_seconds_ = std::max(0.0f, kFadeDelaySeconds - ok_idle_timer_);
    }
    else
    {
        ok_idle_timer_ = 0.0f;
        fade_alpha_ = 1.0f;
        countdown_seconds_ = 0.0f;
    }
}
