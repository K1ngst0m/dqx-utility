#include "GlobalSettingsPanel.hpp"

#include "dialog/DialogWindow.hpp"
#include "quest/QuestWindow.hpp"
#include "help/HelpWindow.hpp"
#include "ProcessDetector.hpp"
#include "ProcessLocaleChecker.hpp"
#include "DQXClarityLauncher.hpp"
#include "config/ConfigManager.hpp"
#include "UITheme.hpp"
#include "DQXClarityService.hpp"
#include "dqxclarity/api/dqxclarity.hpp"
#include "ui/Localization.hpp"
#include "ui/DockState.hpp"
#include "UIHelper.hpp"
#include "utils/ErrorReporter.hpp"

#include <algorithm>
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <map>
#include <unordered_set>

namespace
{
struct WindowTypeEntry
{
    UIWindowType type;
    const char* label_key;
};

constexpr WindowTypeEntry kWindowTypes[] = {
    { UIWindowType::Dialog, "window_type.dialog" },
    { UIWindowType::Quest,  "window_type.quest"  },
    { UIWindowType::Help,   "window_type.help"   }
};

const char* windowTypeLabel(UIWindowType type)
{
    for (const auto& entry : kWindowTypes)
    {
        if (entry.type == type)
        {
            return i18n::get(entry.label_key);
        }
    }
    return "";
}

const char* severityBadge(utils::ErrorSeverity severity)
{
    switch (severity)
    {
    case utils::ErrorSeverity::Info:
        return "[i]";
    case utils::ErrorSeverity::Warning:
        return "[!]";
    case utils::ErrorSeverity::Error:
        return "[x]";
    case utils::ErrorSeverity::Fatal:
        return "[!!]";
    default:
        return "";
    }
}

ImVec4 severityColor(utils::ErrorSeverity severity)
{
    switch (severity)
    {
    case utils::ErrorSeverity::Info:
        return ImVec4(0.35f, 0.65f, 0.95f, 1.0f);
    case utils::ErrorSeverity::Warning:
        return UITheme::warningColor();
    case utils::ErrorSeverity::Error:
        return UITheme::errorColor();
    case utils::ErrorSeverity::Fatal:
        return ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
    default:
        return UITheme::disabledColor();
    }
}

const char* addButtonLabel(UIWindowType type)
{
    switch (type)
    {
    case UIWindowType::Dialog:
        return i18n::get("settings.add_dialog");
    case UIWindowType::Quest:
        return i18n::get("settings.add_quest");
    case UIWindowType::Help:
        return i18n::get("settings.add_help");
    }
    return nullptr;
}

std::vector<UIWindow*> collectAllWindows(WindowRegistry& registry)
{
    std::vector<UIWindow*> result;
    result.reserve(registry.windows().size());
    for (auto& window : registry.windows())
    {
        result.push_back(window.get());
    }
    return result;
}
} // namespace

// Builds a settings panel tied to the window registry.
GlobalSettingsPanel::GlobalSettingsPanel(WindowRegistry& registry)
    : registry_(registry)
    , dqxc_launcher_(std::make_unique<DQXClarityLauncher>())
{
    // Expose launcher globally for UI windows to fetch dialog messages
    DQXClarityService_Set(dqxc_launcher_.get());
}

// Renders the settings window with type/instance selectors.
void GlobalSettingsPanel::render(bool& open)
{
    if (!open)
        return;

    if (DockState::IsScattering())
    {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(DockState::NextScatterPos(), ImGuiCond_Always);
    }
    else if (auto* cm = ConfigManager_Get())
    {
        if (cm->getAppMode() == ConfigManager::AppMode::Mini)
        {
            ImGui::SetNextWindowDockID(DockState::GetDockspace(), ImGuiCond_Always);
        }
    }

    if (auto* cm = ConfigManager_Get())
    {
        if (cm->getAppMode() != ConfigManager::AppMode::Mini && DockState::ShouldReDock())
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_Always);
        }
        else
        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_FirstUseEver);
        }
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f), ImGuiCond_FirstUseEver);
    }
    UITheme::pushSettingsWindowStyle();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (auto* cm2 = ConfigManager_Get())
    {
        if (cm2->getAppMode() == ConfigManager::AppMode::Mini)
            flags |= ImGuiWindowFlags_NoMove;
    }
    if (ImGui::Begin((std::string(i18n::get("settings.title")) + "###global_settings").c_str(), &open, flags))
    {
        renderStatusSection();

        renderAppearanceSection();

#ifdef DQXU_ENABLE_DEBUG_SECTIONS
        renderWindowManagementSection();
        if (ImGui::CollapsingHeader(i18n::get("settings.sections.debug")))
        {
            renderDebugSection();
        }
#endif
    }
    ImGui::End();

    UITheme::popSettingsWindowStyle();
}

// Provides a combo box for selecting the active window type.
void GlobalSettingsPanel::renderTypeSelector()
{
    int current_index = 0;
    for (int i = 0; i < static_cast<int>(std::size(kWindowTypes)); ++i)
    {
        if (kWindowTypes[i].type == selected_type_)
        {
            current_index = i;
            break;
        }
    }

    const char* preview = windowTypeLabel(selected_type_);
    if (ImGui::BeginCombo("##window_type_combo", preview))
    {
        for (int i = 0; i < static_cast<int>(std::size(kWindowTypes)); ++i)
        {
            const bool selected = (i == current_index);
            const char* label = i18n::get(kWindowTypes[i].label_key);
            if (ImGui::Selectable(label, selected))
            {
                selected_type_ = kWindowTypes[i].type;
                previous_selected_index_ = -1;

                int new_index = 0;
                bool found = false;
                int idx = 0;
                for (auto& window : registry_.windows())
                {
                    if (window->type() == selected_type_)
                    {
                        new_index = idx;
                        found = true;
                        break;
                    }
                    ++idx;
                }
                selected_index_ = found ? new_index : 0;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Displays instance list and creation controls for the active type.
void GlobalSettingsPanel::renderInstanceSelector()
{
    UIWindow* newly_created = nullptr;

    if (const char* add_label = addButtonLabel(selected_type_))
    {
        if (ImGui::Button(add_label))
        {
            switch (selected_type_)
            {
            case UIWindowType::Dialog:
                newly_created = &registry_.createDialogWindow();
                break;
            case UIWindowType::Quest:
                newly_created = &registry_.createQuestWindow();
                break;
            case UIWindowType::Help:
                newly_created = &registry_.createHelpWindow();
                break;
            default:
                break;
            }
            previous_selected_index_ = -1;
        }
        ImGui::SameLine();
    }

    auto windows = collectAllWindows(registry_);

    if (newly_created)
    {
        for (int i = 0; i < static_cast<int>(windows.size()); ++i)
        {
            if (windows[i] == newly_created)
            {
                selected_index_ = i;
                break;
            }
        }
    }

    {
        std::string total = i18n::format("total", {
                                                      { "count", std::to_string(windows.size()) }
        });
        ImGui::TextDisabled("%s", total.c_str());
    }

    if (windows.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", i18n::get("settings.no_instances"));
        return;
    }

    if (selected_index_ >= static_cast<int>(windows.size()))
        selected_index_ = static_cast<int>(windows.size()) - 1;
    if (selected_index_ < 0)
        selected_index_ = 0;

    if (ImGui::BeginTable("InstanceTable", 3, ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn(i18n::get("settings.table.name"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(i18n::get("settings.table.type"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(i18n::get("settings.table.actions"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        int quest_count = static_cast<int>(registry_.windowsByType(UIWindowType::Quest).size());

        for (int i = 0; i < static_cast<int>(windows.size()); ++i)
        {
            UIWindow* win = windows[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool selected = (i == selected_index_);
            if (ImGui::Selectable(win->displayName(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                selected_index_ = i;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(windowTypeLabel(win->type()));

            ImGui::TableSetColumnIndex(2);
            std::string remove_id = std::string(i18n::get("common.remove")) + "##" + win->windowLabel();
            bool disable_remove = (win->type() == UIWindowType::Quest && quest_count <= 1);
            if (disable_remove)
                ImGui::BeginDisabled();
            if (ImGui::SmallButton(remove_id.c_str()))
            {
                registry_.removeWindow(win);

                // Update the windows list after removal
                auto updated_windows = collectAllWindows(registry_);

                // Reset state after removal
                if (updated_windows.empty())
                {
                    selected_index_ = 0;
                }
                else
                {
                    // If we removed the selected window, clamp to valid range
                    selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(updated_windows.size()) - 1);
                }

                previous_selected_index_ = -1;
                rename_buffer_.fill('\0');

                ImGui::EndTable();
                if (disable_remove)
                    ImGui::EndDisabled();
                return;
            }
            if (disable_remove)
                ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

    if (selected_index_ != previous_selected_index_)
    {
        previous_selected_index_ = selected_index_;
        rename_buffer_.fill('\0');
        UIWindow* current = windows[selected_index_];
        std::snprintf(rename_buffer_.data(), rename_buffer_.size(), "%s", current->displayName());
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(i18n::get("settings.rename_instance"));
    ImGui::InputText("##instance_rename", rename_buffer_.data(), rename_buffer_.size());
    ImGui::SameLine();
    if (ImGui::Button(i18n::get("apply")))
    {
        UIWindow* current = windows[selected_index_];
        current->rename(rename_buffer_.data());
        std::snprintf(rename_buffer_.data(), rename_buffer_.size(), "%s", current->displayName());
    }
}

void GlobalSettingsPanel::renderDQXClaritySection()
{
    if (!dqxc_launcher_)
        return;

    // Get current status
    DQXClarityStatus status = dqxc_launcher_->getStatus();
    std::string status_str = dqxc_launcher_->getStatusString();

    // Determine color based on status
    ImVec4 status_color;
    switch (status)
    {
    case DQXClarityStatus::Running:
        status_color = UITheme::successColor();
        break;
    case DQXClarityStatus::Connected:
        status_color = UITheme::successColor();
        break;
    case DQXClarityStatus::Disconnected:
        status_color = UITheme::errorColor();
        break;
    case DQXClarityStatus::Stopped:
        status_color = UITheme::disabledColor();
        break;
    }

    // Display concise status only
    ImGui::TextColored(status_color, "●");
    ImGui::SameLine();
    ImGui::TextUnformatted(i18n::get("settings.dqxc.status_label"));
    ImGui::SameLine();
    ImGui::TextColored(status_color, "%s", status_str.c_str());

    ImGui::Spacing();

    // Compatibility mode checkbox
    auto* config_mgr = ConfigManager_Get();
    if (config_mgr)
    {
        bool compat_mode = config_mgr->getCompatibilityMode();
        bool is_hooked = (status == DQXClarityStatus::Connected);

        if (ImGui::Checkbox(i18n::get("settings.dqxc.compatibility_mode"), &compat_mode))
        {
            config_mgr->setCompatibilityMode(compat_mode);
            ConfigManager_SaveAll();

            // Reinitialize engine with new compatibility mode setting
            if (dqxc_launcher_)
            {
                dqxc_launcher_->reinitialize();
            }
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", i18n::get("settings.dqxc.compatibility_mode_tooltip"));
        }
    }

#ifndef _WIN32
    // Show wineserver info on Linux
    if (status == DQXClarityStatus::Disconnected)
    {
        ImGui::Spacing();
        ImGui::TextColored(UITheme::errorColor(), "%s", i18n::get("common.warning"));
        ImGui::TextWrapped("%s", i18n::get("settings.dqxc.wineserver_mismatch"));
    }
#endif
}

void GlobalSettingsPanel::renderDebugSection()
{
    ImGui::TextUnformatted(i18n::get("settings.dqxc.debug_title"));

    if (dqxc_launcher_)
    {
        // Launch/Stop moved to Debug; guard against mid-stages
        auto stage = dqxc_launcher_->getEngineStage();
        bool is_busy = (stage != dqxclarity::Status::Stopped && stage != dqxclarity::Status::Hooked);
        bool is_stopped = (stage == dqxclarity::Status::Stopped);
        bool dqx_running = dqxc_launcher_->isDQXGameRunning();

        if (is_stopped)
        {
            bool disable = is_busy || !dqx_running;
            if (disable)
                ImGui::BeginDisabled();
            std::string label = std::string(i18n::get("common.start")) + "##dqxc_dbg";
            if (ImGui::Button(label.c_str(), ImVec2(120, 0)))
            {
                dqxc_launcher_->launch();
            }
            if (disable)
            {
                ImGui::EndDisabled();
                if (!dqx_running)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", i18n::get("settings.dqxc.not_running_hint"));
                }
            }
        }
        else
        {
            bool disable = is_busy; // disable Stop during Starting/Stopping
            if (disable)
                ImGui::BeginDisabled();
            std::string label = std::string(i18n::get("common.stop")) + "##dqxc_dbg";
            if (ImGui::Button(label.c_str(), ImVec2(120, 0)))
            {
                dqxc_launcher_->stop();
            }
            if (disable)
                ImGui::EndDisabled();
        }
    }

    renderProblemsPanel();

    ImGui::SeparatorText(i18n::get("settings.logs"));

    // Refresh log content every 2 seconds
    float current_time = static_cast<float>(ImGui::GetTime());
    if (current_time - last_log_refresh_time_ > 2.0f)
    {
        // Read the app's own log from current working directory
        std::string log_path = "logs/run.log";
        cached_log_content_ = readLogFile(log_path);
        last_log_refresh_time_ = current_time;
    }

    // Display log content in a read-only text box
    ImGui::InputTextMultiline("##dqxc_logs", const_cast<char*>(cached_log_content_.c_str()), cached_log_content_.size(),
                              ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);
}

void GlobalSettingsPanel::renderProblemsPanel()
{
    ImGui::SeparatorText("Problems");

    auto history = utils::ErrorReporter::GetHistorySnapshot();

    if (ImGui::Button("Clear All##problems_clear"))
    {
        utils::ErrorReporter::ClearHistory();
        history.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entries", history.size());

    if (history.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("No issues reported.");
        return;
    }

    std::map<utils::ErrorCategory, std::vector<const utils::ErrorReport*>> grouped;
    for (const auto& report : history)
    {
        grouped[report.category].push_back(&report);
    }

    constexpr std::array<utils::ErrorCategory, 7> kOrderedCategories = {
        utils::ErrorCategory::Initialization, utils::ErrorCategory::MemoryHook, utils::ErrorCategory::ProcessDetection,
        utils::ErrorCategory::Configuration,  utils::ErrorCategory::IPC,        utils::ErrorCategory::Translation,
        utils::ErrorCategory::Unknown
    };

    std::unordered_set<utils::ErrorCategory> rendered;

    const auto renderCategory = [&](utils::ErrorCategory category, const std::vector<const utils::ErrorReport*>& items)
    {
        if (items.empty())
            return;

        std::string header = utils::ErrorReporter::CategoryToString(category);
        header += " (" + std::to_string(items.size()) + ")";
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
            std::string table_id = "ProblemsTable_" + std::to_string(static_cast<int>(category));
            if (ImGui::BeginTable(table_id.c_str(), 4, table_flags))
            {
                ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 135.0f);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);

                int idx = 0;
                for (const auto* entry : items)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID(idx++);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(severityColor(entry->severity), "%s", severityBadge(entry->severity));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry->timestamp.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", entry->user_message.c_str());
                    if (!entry->technical_details.empty() && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(entry->technical_details.c_str());
                        ImGui::EndTooltip();
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Copy"))
                    {
                        std::string clip = "[" + entry->timestamp + "] " + entry->user_message;
                        if (!entry->technical_details.empty())
                        {
                            clip += " | ";
                            clip += entry->technical_details;
                        }
                        ImGui::SetClipboardText(clip.c_str());
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        }
        rendered.insert(category);
    };

    for (auto category : kOrderedCategories)
    {
        auto it = grouped.find(category);
        if (it != grouped.end())
        {
            renderCategory(category, it->second);
        }
    }

    for (const auto& [category, items] : grouped)
    {
        if (rendered.count(category) == 0)
        {
            renderCategory(category, items);
        }
    }
}

std::string GlobalSettingsPanel::readLogFile(const std::string& path, size_t max_lines)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return i18n::format("settings.log_viewer.not_found", {
                                                                 { "path", path }
        });
    }

    std::vector<std::string> lines;
    std::string line;

    // Read all lines
    while (std::getline(file, line))
    {
        lines.push_back(line);
    }

    // Keep only the last max_lines
    size_t start_idx = 0;
    if (lines.size() > max_lines)
    {
        start_idx = lines.size() - max_lines;
    }

    // Join lines
    std::ostringstream oss;
    for (size_t i = start_idx; i < lines.size(); ++i)
    {
        oss << lines[i];
        if (i < lines.size() - 1)
            oss << "\n";
    }

    return oss.str();
}

void GlobalSettingsPanel::renderStatusSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.status"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool dqx_running = ProcessDetector::isProcessRunning("DQXGame.exe");
        ImVec4 game_status_color = dqx_running ? UITheme::successColor() : UITheme::errorColor();
        const char* game_status_text =
            dqx_running ? i18n::get("settings.status.running") : i18n::get("settings.status.not_running");

        // DQX Game Status
        ImGui::TextColored(game_status_color, "●");
        ImGui::SameLine();
        ImGui::TextUnformatted(i18n::get("settings.status.game_label"));
        ImGui::SameLine();
        ImGui::TextColored(game_status_color, "%s", game_status_text);

#ifdef _WIN32
        // Locale Status (Windows only) - based on window title
        if (dqx_running)
        {
            ProcessLocale locale = ProcessLocaleChecker::checkProcessLocale("DQXGame.exe");
            ImVec4 locale_color;
            const char* locale_text;

            switch (locale)
            {
            case ProcessLocale::Japanese:
                locale_color = UITheme::successColor();
                locale_text = i18n::get("settings.status.japanese");
                break;
            case ProcessLocale::NonJapanese:
                locale_color = UITheme::warningColor();
                locale_text = i18n::get("settings.status.non_japanese");
                break;
            case ProcessLocale::Unknown:
            default:
                locale_color = UITheme::disabledColor();
                locale_text = i18n::get("settings.status.unknown");
                break;
            }

            ImGui::TextColored(locale_color, "●");
            ImGui::SameLine();
            ImGui::TextUnformatted(i18n::get("settings.status.locale_label"));
            ImGui::SameLine();
            ImGui::TextColored(locale_color, "%s", locale_text);
        }
#endif

        renderDQXClaritySection();
    }
}

void GlobalSettingsPanel::renderWindowManagementSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.window_management")))
    {
        if (auto* cm = ConfigManager_Get())
        {
            bool default_dialog = cm->isDefaultDialogEnabled();
            std::string dialog_label =
                ui::LocalizedOrFallback("settings.window.default_dialog", "Default dialog window");
            if (ImGui::Checkbox(dialog_label.c_str(), &default_dialog))
            {
                cm->setDefaultDialogEnabled(default_dialog);
            }

            bool default_quest = cm->isDefaultQuestEnabled();
            std::string quest_label = ui::LocalizedOrFallback("settings.window.default_quest", "Default quest window");
            if (ImGui::Checkbox(quest_label.c_str(), &default_quest))
            {
                cm->setDefaultQuestEnabled(default_quest);
            }

            ImGui::Spacing();
        }
        ImGui::TextUnformatted(i18n::get("settings.window_type"));
        renderTypeSelector();
        ImGui::Spacing();

        renderInstanceSelector();
    }
}

void GlobalSettingsPanel::renderAppearanceSection()
{
    if (ImGui::CollapsingHeader(i18n::get("settings.sections.appearance"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        float ui_scale = 1.0f;
        if (auto* cm = ConfigManager_Get())
            ui_scale = cm->getUIScale();
        ImGui::TextUnformatted(i18n::get("settings.ui_scale"));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderFloat("##ui_scale_slider", &ui_scale, 0.75f, 2.0f, "%.2fx"))
        {
            if (auto* cm = ConfigManager_Get())
                cm->setUIScale(ui_scale);
        }

        // UI Language selector (proof of concept)
        ImGui::TextUnformatted(i18n::get("settings.ui_language.label"));
        const char* langs[] = { i18n::get("settings.ui_language.option_en"),
                                i18n::get("settings.ui_language.option_zh_cn") };
        int idx = 0;
        if (auto* cm = ConfigManager_Get())
        {
            const char* code = cm->getUILanguageCode();
            if (code && std::string(code) == "zh-CN")
                idx = 1;
        }
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##ui_lang_combo", &idx, langs, IM_ARRAYSIZE(langs)))
        {
            const char* new_code = (idx == 1) ? "zh-CN" : "en";
            if (auto* cm = ConfigManager_Get())
                cm->setUILanguageCode(new_code);
            i18n::set_language(new_code);
        }

        ImGui::TextUnformatted(i18n::get("settings.app_mode.label"));
        const char* app_modes[] = { i18n::get("settings.app_mode.items.normal"),
                                    i18n::get("settings.app_mode.items.borderless"),
                                    i18n::get("settings.app_mode.items.mini") };
        int app_mode_idx = 0;
        if (auto* cm = ConfigManager_Get())
            app_mode_idx = static_cast<int>(cm->getAppMode());
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##app_mode_combo", &app_mode_idx, app_modes, IM_ARRAYSIZE(app_modes)))
        {
            if (auto* cm = ConfigManager_Get())
                cm->setAppMode(static_cast<ConfigManager::AppMode>(app_mode_idx));
        }

        if (auto* cm = ConfigManager_Get())
        {
            if (cm->getAppMode() == ConfigManager::AppMode::Normal)
            {
                bool always_on_top = cm->getWindowAlwaysOnTop();
                if (ImGui::Checkbox(i18n::get("settings.always_on_top"), &always_on_top))
                {
                    cm->setWindowAlwaysOnTop(always_on_top);
                }
            }
        }
    }
}
