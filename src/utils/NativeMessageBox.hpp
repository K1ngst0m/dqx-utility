#pragma once

#include <string>

namespace utils {

/**
 * @brief Native platform message box for displaying errors before ImGui is available
 * 
 * This is used for critical initialization failures where ImGui cannot be used yet.
 * Uses Windows MessageBoxW on Windows, zenity/console on Linux.
 */
class NativeMessageBox
{
public:
    enum class Type
    {
        Error,
        Warning,
        Info
    };

    /**
     * @brief Show a native error message box
     * @param title Message box title
     * @param message Message content
     * @param type Message type (error, warning, info)
     */
    static void Show(const std::string& title, const std::string& message, Type type = Type::Error);

    /**
     * @brief Show a fatal error message before exiting
     * @param message Error message to display
     * @param details Optional technical details
     */
    static void ShowFatalError(const std::string& message, const std::string& details = "");
};

} // namespace utils
