#include "NativeMessageBox.hpp"
#include <sstream>
#include <iostream>
#include "ui/Localization.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <codecvt>
#include <locale>
#endif

namespace utils
{

#ifdef _WIN32
static std::wstring StringToWString(const std::string& str)
{
    if (str.empty())
        return std::wstring();

    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}
#endif

void NativeMessageBox::Show(const std::string& title, const std::string& message, Type type)
{
#ifdef _WIN32
    UINT mb_type = MB_OK;
    switch (type)
    {
    case Type::Error:
        mb_type |= MB_ICONERROR;
        break;
    case Type::Warning:
        mb_type |= MB_ICONWARNING;
        break;
    case Type::Info:
        mb_type |= MB_ICONINFORMATION;
        break;
    }

    std::wstring wtitle = StringToWString(title);
    std::wstring wmessage = StringToWString(message);
    MessageBoxW(NULL, wmessage.c_str(), wtitle.c_str(), mb_type);
#else
    // Linux: Try zenity, fall back to console
    std::stringstream cmd;
    std::string type_str;
    switch (type)
    {
    case Type::Error:
        type_str = "--error";
        break;
    case Type::Warning:
        type_str = "--warning";
        break;
    case Type::Info:
        type_str = "--info";
        break;
    }

    cmd << "zenity " << type_str << " --title=\"" << title << "\" --text=\"" << message << "\" 2>/dev/null";
    int result = system(cmd.str().c_str());

    // If zenity failed, print to console
    if (result != 0)
    {
        std::cerr << "\n========================================\n";
        std::cerr << title << "\n";
        std::cerr << "========================================\n";
        std::cerr << message << "\n";
        std::cerr << "========================================\n";
    }
#endif
}

void NativeMessageBox::ShowFatalError(const std::string& message, const std::string& details)
{
    std::stringstream ss;
    ss << message << "\n\n";

    if (!details.empty())
    {
        ss << i18n::get("error.native.technical_details") << "\n" << details << "\n\n";
    }

    ss << i18n::get("error.native.exit_line") << "\n";
    ss << i18n::get("error.native.check_logs");

    Show(i18n::get("error.native.fatal_title"), ss.str(), Type::Error);
}

} // namespace utils
