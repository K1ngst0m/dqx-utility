#include "ErrorDialog.hpp"
#include <SDL3/SDL.h>
#include <sstream>
#include "ui/Localization.hpp"

static const char* LocalizeSeverity(utils::ErrorSeverity sev)
{
    using utils::ErrorSeverity;
    switch (sev)
    {
    case ErrorSeverity::Info:
        return i18n::get("error.severity.info");
    case ErrorSeverity::Warning:
        return i18n::get("error.severity.warning");
    case ErrorSeverity::Error:
        return i18n::get("error.severity.error");
    case ErrorSeverity::Fatal:
        return i18n::get("error.severity.fatal");
    default:
        return i18n::get("error.severity.unknown");
    }
}

static const char* LocalizeCategory(utils::ErrorCategory cat)
{
    using utils::ErrorCategory;
    switch (cat)
    {
    case ErrorCategory::Initialization:
        return i18n::get("error.category.initialization");
    case ErrorCategory::MemoryHook:
        return i18n::get("error.category.memory_hook");
    case ErrorCategory::ProcessDetection:
        return i18n::get("error.category.process_detection");
    case ErrorCategory::Configuration:
        return i18n::get("error.category.configuration");
    case ErrorCategory::IPC:
        return i18n::get("error.category.ipc");
    case ErrorCategory::Translation:
        return i18n::get("error.category.translation");
    case ErrorCategory::Unknown:
        return i18n::get("error.category.unknown");
    default:
        return i18n::get("error.category.unknown");
    }
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

ErrorDialog::ErrorDialog()
    : is_open_(false)
    , selected_error_(0)
    , show_technical_details_(false)
{
}

void ErrorDialog::Show(const std::vector<utils::ErrorReport>& errors)
{
    if (errors.empty())
        return;

    current_errors_ = errors;
    selected_error_ = 0;
    show_technical_details_ = false;
    is_open_ = true;

    // Open the modal with a stable ID while showing localized title
    std::string popup_title = std::string(i18n::get("error.title")) + "###error_report_modal";
    ImGui::OpenPopup(popup_title.c_str());
}

bool ErrorDialog::Render()
{
    if (!is_open_)
        return false;

    bool should_exit = false;

    // Center the modal
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

    std::string popup_title = std::string(i18n::get("error.title")) + "###error_report_modal";
    if (ImGui::BeginPopupModal(popup_title.c_str(), &is_open_, ImGuiWindowFlags_NoCollapse))
    {
        if (current_errors_.empty())
        {
            ImGui::EndPopup();
            return false;
        }

        const auto& error = current_errors_[selected_error_];

        // Severity header with icon and color
        ImGui::PushStyleColor(ImGuiCol_Text, GetSeverityColor(error.severity));
        ImGui::TextUnformatted(GetSeverityIcon(error.severity));
        ImGui::SameLine();
        ImGui::Text("%s - %s", LocalizeSeverity(error.severity), LocalizeCategory(error.category));
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Timestamp
        ImGui::TextDisabled("%s %s", i18n::get("error.time"), error.timestamp.c_str());
        ImGui::Spacing();

        // User message (wrapped)
        ImGui::TextWrapped("%s", error.user_message.c_str());
        ImGui::Spacing();

        // Technical details (collapsible)
        if (!error.technical_details.empty())
        {
            if (ImGui::CollapsingHeader(i18n::get("error.technical_details"),
                                        show_technical_details_ ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            {
                show_technical_details_ = true;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("TechnicalDetails", ImVec2(0, 150), true);
                ImGui::TextWrapped("%s", error.technical_details.c_str());
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            else
            {
                show_technical_details_ = false;
            }
        }

        ImGui::Spacing();

        // Multiple errors navigation
        if (current_errors_.size() > 1)
        {
            ImGui::Separator();
            {
                std::string counter =
                    i18n::format("error.counter", {
                                                      { "index", std::to_string(selected_error_ + 1)    },
                                                      { "total", std::to_string(current_errors_.size()) }
                });
                ImGui::TextUnformatted(counter.c_str());
            }
            ImGui::SameLine();

            if (ImGui::Button(i18n::get("error.prev")) && selected_error_ > 0)
            {
                selected_error_--;
                show_technical_details_ = false;
            }
            ImGui::SameLine();

            if (ImGui::Button(i18n::get("error.next")) &&
                selected_error_ < static_cast<int>(current_errors_.size()) - 1)
            {
                selected_error_++;
                show_technical_details_ = false;
            }
        }

        ImGui::Separator();

        // Action buttons
        if (ImGui::Button(i18n::get("error.copy_to_clipboard")))
        {
            CopyToClipboard(error);
        }
        ImGui::SameLine();

        if (ImGui::Button(i18n::get("error.open_logs_folder")))
        {
            OpenLogsFolder();
        }
        ImGui::SameLine();

        // Exit or Continue button
        if (error.is_fatal)
        {
            if (ImGui::Button(i18n::get("error.exit_application")))
            {
                should_exit = true;
                Close();
            }
        }
        else
        {
            if (ImGui::Button(i18n::get("error.continue")))
            {
                Close();
            }
        }

        ImGui::EndPopup();
    }

    return should_exit;
}

void ErrorDialog::Close()
{
    is_open_ = false;
    current_errors_.clear();
    selected_error_ = 0;
    show_technical_details_ = false;
}

void ErrorDialog::CopyToClipboard(const utils::ErrorReport& error)
{
    std::string formatted = FormatErrorReport(error);
    SDL_SetClipboardText(formatted.c_str());
}

void ErrorDialog::OpenLogsFolder()
{
#ifdef _WIN32
    // Use ShellExecute to open logs folder
    ShellExecuteW(NULL, L"open", L"logs", NULL, NULL, SW_SHOWNORMAL);
#else
    // Linux: use xdg-open
    system("xdg-open logs 2>/dev/null &");
#endif
}

const char* ErrorDialog::GetSeverityIcon(utils::ErrorSeverity severity)
{
    switch (severity)
    {
    case utils::ErrorSeverity::Info:
        return "[i]";
    case utils::ErrorSeverity::Warning:
        return "[!]";
    case utils::ErrorSeverity::Error:
        return "[X]";
    case utils::ErrorSeverity::Fatal:
        return "[!!]";
    default:
        return "[?]";
    }
}

ImVec4 ErrorDialog::GetSeverityColor(utils::ErrorSeverity severity)
{
    switch (severity)
    {
    case utils::ErrorSeverity::Info:
        return ImVec4(0.5f, 0.8f, 1.0f, 1.0f); // Light blue
    case utils::ErrorSeverity::Warning:
        return ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Yellow
    case utils::ErrorSeverity::Error:
        return ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange
    case utils::ErrorSeverity::Fatal:
        return ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Red
    default:
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
    }
}

std::string ErrorDialog::FormatErrorReport(const utils::ErrorReport& error)
{
    std::stringstream ss;
    ss << "=====================================\n";
    ss << "DQX Utility Error Report\n";
    ss << "=====================================\n";
    ss << "Time: " << error.timestamp << "\n";
    ss << "Severity: " << utils::ErrorReporter::SeverityToString(error.severity) << "\n";
    ss << "Category: " << utils::ErrorReporter::CategoryToString(error.category) << "\n";
    ss << "\n";
    ss << "Message:\n";
    ss << error.user_message << "\n";

    if (!error.technical_details.empty())
    {
        ss << "\n";
        ss << "Technical Details:\n";
        ss << error.technical_details << "\n";
    }

    ss << "\n";
    ss << "Please check logs/run.log for more information.\n";
    ss << "=====================================\n";

    return ss.str();
}
