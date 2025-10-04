#include "Console.hpp"

namespace dqxclarity {

std::wstring Console::ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed == 0) {
        cp = GetACP();
        flags = 0;
        needed = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
        if (needed == 0) return L"";
    }
    std::wstring w; w.resize(needed);
    MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), w.data(), needed);
    return w;
}

void Console::PrintDialog(const std::string& npc, const std::string& text) {
    std::wstring line = L"Dialog captured: [" + ToWide(npc) + L"] " + ToWide(text) + L"\n";
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &written, nullptr);
    }
}

void Console::PrintInfo(const std::string& line) {
    std::wstring w = ToWide(line + "\n");
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleW(h, w.c_str(), (DWORD)w.size(), &written, nullptr);
    }
}

} // namespace dqxclarity
