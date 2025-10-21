#include "SingleInstanceGuard.hpp"

#include "../utils/ErrorReporter.hpp"
#include "ProcessDetector.hpp"

#include <plog/Log.h>

#include <filesystem>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{

#ifdef _WIN32
std::filesystem::path GetExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0)
        return {};

    while (size == buffer.size())
    {
        buffer.resize(buffer.size() * 2, L'\0');
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return {};
    }
    buffer.resize(size);

    return std::filesystem::path(buffer);
}

std::wstring NormalizePathLower(const std::filesystem::path& path)
{
    std::wstring normalized;
    try
    {
        auto canonical = std::filesystem::weakly_canonical(path);
        normalized = canonical.wstring();
    }
    catch (const std::exception&)
    {
        normalized = path.lexically_normal().wstring();
    }

    for (auto& ch : normalized)
    {
        if (ch >= L'A' && ch <= L'Z')
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return normalized;
}

std::wstring BuildMutexName(const std::filesystem::path& exe_path)
{
    constexpr wchar_t kMutexBaseName[] = L"Global\\DQXUtilityInstance";

    auto parent = exe_path.parent_path();
    if (parent.empty())
        return kMutexBaseName;

    auto normalized_parent = NormalizePathLower(parent);

    // FNV-1a 64-bit
    std::uint64_t hash = 1469598103934665603ULL;
    for (wchar_t ch : normalized_parent)
    {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }

    wchar_t hash_buffer[17] = {};
    _snwprintf_s(hash_buffer, _countof(hash_buffer), _TRUNCATE, L"%016llx", static_cast<unsigned long long>(hash));

    std::wstring name(kMutexBaseName);
    name.push_back(L'-');
    name.append(hash_buffer);
    return name;
}

std::string WStringToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};

    int size =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<std::size_t>(size), '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size,
                                      nullptr, nullptr);
    if (written <= 0)
        return {};

    return result;
}
#endif

} // namespace

SingleInstanceGuard::SingleInstanceGuard() = default;

#ifdef _WIN32
SingleInstanceGuard::SingleInstanceGuard(void* handle, std::wstring name)
    : mutex_handle_(handle)
    , mutex_name_(std::move(name))
{
}
#endif

SingleInstanceGuard::~SingleInstanceGuard()
{
#ifdef _WIN32
    if (mutex_handle_)
    {
        ReleaseMutex(static_cast<HANDLE>(mutex_handle_));
        CloseHandle(static_cast<HANDLE>(mutex_handle_));
    }
#endif
}

std::unique_ptr<SingleInstanceGuard> SingleInstanceGuard::Acquire()
{
#ifdef _WIN32
    auto exe_path = GetExecutablePath();
    auto mutex_name = BuildMutexName(exe_path);

    if (!exe_path.empty())
    {
        auto exe_name_utf8 = WStringToUtf8(exe_path.filename().wstring());
        if (!exe_name_utf8.empty() && ProcessDetector::isAnotherDQXU(exe_name_utf8))
        {
            PLOG_WARNING << "Another DQX Utility instance is already running (process-name check).";
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization, "Application already running",
                                                "Another DQX Utility instance is already active.");
            SetLastError(ERROR_ALREADY_EXISTS);
            return nullptr;
        }
    }

    SECURITY_DESCRIPTOR sd;
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
    {
        DWORD err = GetLastError();
        PLOG_ERROR << "InitializeSecurityDescriptor failed: " << err;
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Initialization, "Single instance guard failure",
                                          "InitializeSecurityDescriptor failed with error " + std::to_string(err));
        SetLastError(err);
        return nullptr;
    }

    if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE))
    {
        DWORD err = GetLastError();
        PLOG_ERROR << "SetSecurityDescriptorDacl failed: " << err;
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Initialization, "Single instance guard failure",
                                          "SetSecurityDescriptorDacl failed with error " + std::to_string(err));
        SetLastError(err);
        return nullptr;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    HANDLE mutex = CreateMutexW(&sa, TRUE, mutex_name.c_str());
    if (!mutex)
    {
        DWORD err = GetLastError();
        PLOG_ERROR << "CreateMutexW failed: " << err;
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Initialization, "Single instance guard failure",
                                          "CreateMutexW failed with error " + std::to_string(err));
        SetLastError(err);
        return nullptr;
    }

    DWORD create_error = GetLastError();
    if (create_error == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        PLOG_WARNING << "Another DQX Utility instance is already running.";
        utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization, "Application already running",
                                            "Another DQX Utility instance is already active.");
        SetLastError(create_error);
        return nullptr;
    }

    return std::unique_ptr<SingleInstanceGuard>(new SingleInstanceGuard(mutex, std::move(mutex_name)));
#else
    std::error_code ec;
    auto exe_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
    {
        auto exe_name = exe_path.filename().string();
        if (!exe_name.empty() && ProcessDetector::isAnotherDQXU(exe_name))
        {
            PLOG_WARNING << "Another DQX Utility instance is already running (process-name check).";
            utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Initialization, "Application already running",
                                                "Another DQX Utility instance is already active.");
            return nullptr;
        }
    }

    return std::unique_ptr<SingleInstanceGuard>(new SingleInstanceGuard());
#endif
}
