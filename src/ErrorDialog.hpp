#pragma once

#include "../utils/ErrorReporter.hpp"
#include <imgui.h>
#include <vector>
#include <string>

/**
 * @brief ImGui modal dialog for displaying errors to users
 * 
 * Features:
 * - User-friendly error message display
 * - Collapsible technical details
 * - "Copy to Clipboard" button
 * - "Open Logs Folder" button
 * - Multiple error display support
 * - Appropriate severity icons
 */
class ErrorDialog
{
public:
    ErrorDialog();
    ~ErrorDialog() = default;

    /**
     * @brief Show error dialog with pending errors from ErrorReporter
     * @param errors Vector of error reports to display
     */
    void Show(const std::vector<utils::ErrorReport>& errors);

    /**
     * @brief Render the error dialog (call in main loop)
     * @return true if dialog is open and should exit app (for fatal errors)
     */
    bool Render();

    /**
     * @brief Check if dialog is currently visible
     */
    bool IsOpen() const { return is_open_; }

    /**
     * @brief Close the dialog
     */
    void Close();

private:
    bool is_open_;
    std::vector<utils::ErrorReport> current_errors_;
    int selected_error_;
    bool show_technical_details_;

    /**
     * @brief Copy error report to clipboard
     */
    void CopyToClipboard(const utils::ErrorReport& error);

    /**
     * @brief Open logs folder in system file explorer
     */
    void OpenLogsFolder();

    /**
     * @brief Get icon for error severity
     */
    const char* GetSeverityIcon(utils::ErrorSeverity severity);

    /**
     * @brief Get color for error severity
     */
    ImVec4 GetSeverityColor(utils::ErrorSeverity severity);

    /**
     * @brief Format error report as text for clipboard
     */
    std::string FormatErrorReport(const utils::ErrorReport& error);
};
