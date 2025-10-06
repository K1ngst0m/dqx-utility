#include "ErrorDialog.hpp"
#include <SDL3/SDL.h>
#include <sstream>

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

    // Open the modal
    ImGui::OpenPopup("Error Report");
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

    if (ImGui::BeginPopupModal("Error Report", &is_open_, ImGuiWindowFlags_NoCollapse))
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
        ImGui::Text("%s - %s", 
                    utils::ErrorReporter::SeverityToString(error.severity).c_str(),
                    utils::ErrorReporter::CategoryToString(error.category).c_str());
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Timestamp
        ImGui::TextDisabled("Time: %s", error.timestamp.c_str());
        ImGui::Spacing();

        // User message (wrapped)
        ImGui::TextWrapped("%s", error.user_message.c_str());
        ImGui::Spacing();

        // Technical details (collapsible)
        if (!error.technical_details.empty())
        {
            if (ImGui::CollapsingHeader("Technical Details", show_technical_details_ ? ImGuiTreeNodeFlags_DefaultOpen : 0))
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
            ImGui::Text("Error %d of %zu", selected_error_ + 1, current_errors_.size());
            ImGui::SameLine();
            
            if (ImGui::Button("< Previous") && selected_error_ > 0)
            {
                selected_error_--;
                show_technical_details_ = false;
            }
            ImGui::SameLine();
            
            if (ImGui::Button("Next >") && selected_error_ < static_cast<int>(current_errors_.size()) - 1)
            {
                selected_error_++;
                show_technical_details_ = false;
            }
        }

        ImGui::Separator();

        // Action buttons
        if (ImGui::Button("Copy to Clipboard"))
        {
            CopyToClipboard(error);
        }
        ImGui::SameLine();

        if (ImGui::Button("Open Logs Folder"))
        {
            OpenLogsFolder();
        }
        ImGui::SameLine();

        // Exit or Continue button
        if (error.is_fatal)
        {
            if (ImGui::Button("Exit Application"))
            {
                should_exit = true;
                Close();
            }
        }
        else
        {
            if (ImGui::Button("Continue"))
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
    case utils::ErrorSeverity::Info:    return "[i]";
    case utils::ErrorSeverity::Warning: return "[!]";
    case utils::ErrorSeverity::Error:   return "[X]";
    case utils::ErrorSeverity::Fatal:   return "[!!]";
    default:                            return "[?]";
    }
}

ImVec4 ErrorDialog::GetSeverityColor(utils::ErrorSeverity severity)
{
    switch (severity)
    {
    case utils::ErrorSeverity::Info:    return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Light blue
    case utils::ErrorSeverity::Warning: return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
    case utils::ErrorSeverity::Error:   return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
    case utils::ErrorSeverity::Fatal:   return ImVec4(1.0f, 0.2f, 0.2f, 1.0f);  // Red
    default:                            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
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
