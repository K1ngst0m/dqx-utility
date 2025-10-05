#include "ProcessLocaleChecker.hpp"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

// Callback for EnumWindows to find windows by process name
struct EnumWindowsCallbackData
{
    DWORD processId;
    std::wstring windowTitle;
    bool found;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    auto* data = reinterpret_cast<EnumWindowsCallbackData*>(lParam);
    
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    
    if (windowPid == data->processId)
    {
        // Check if this is a visible window with a title
        if (IsWindowVisible(hwnd))
        {
            wchar_t title[256];
            int len = GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
            if (len > 0)
            {
                data->windowTitle = title;
                data->found = true;
                return FALSE;  // Stop enumeration
            }
        }
    }
    
    return TRUE;  // Continue enumeration
}

ProcessLocale ProcessLocaleChecker::checkProcessLocale(const std::string& processName)
{
#ifdef _WIN32
    // Find the process ID
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return ProcessLocale::Unknown;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            // Convert process name to wide string for comparison
            std::wstring wProcessName(processName.begin(), processName.end());
            std::wstring entryName(entry.szExeFile);
            
            // Case-insensitive comparison
            if (_wcsicmp(entryName.c_str(), wProcessName.c_str()) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    if (pid == 0)
        return ProcessLocale::Unknown;

    // Find the window title for this process
    EnumWindowsCallbackData data;
    data.processId = pid;
    data.found = false;
    
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));
    
    if (!data.found || data.windowTitle.empty())
        return ProcessLocale::Unknown;

    // Check if the window title contains Japanese text "ドラゴンクエスト" (Dragon Quest)
    // Using Unicode code points directly to avoid encoding issues
    std::wstring japaneseSubstring = L"\u30C9\u30E9\u30B4\u30F3\u30AF\u30A8\u30B9\u30C8";
    
    if (data.windowTitle.find(japaneseSubstring) != std::wstring::npos)
        return ProcessLocale::Japanese;
    else
        return ProcessLocale::NonJapanese;
#else
    (void)processName;
    return ProcessLocale::Unknown;
#endif
}

#else
// Non-Windows stub
ProcessLocale ProcessLocaleChecker::checkProcessLocale(const std::string& processName)
{
    (void)processName;
    return ProcessLocale::Unknown;
}
#endif
