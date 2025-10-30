#include "ProcessUtils.hpp"

#include <plog/Log.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstring>
#endif

namespace utils
{

std::filesystem::path ProcessUtils::GetExecutablePath()
{
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0)
    {
        PLOG_ERROR << "GetModuleFileNameW failed: " << GetLastError();
        return {};
    }

    while (size == buffer.size())
    {
        buffer.resize(buffer.size() * 2, L'\0');
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
        {
            PLOG_ERROR << "GetModuleFileNameW failed: " << GetLastError();
            return {};
        }
    }
    buffer.resize(size);

    return std::filesystem::path(buffer);
#else
    std::error_code ec;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
    {
        PLOG_ERROR << "Failed to read /proc/self/exe: " << ec.message();
        return {};
    }
    return exePath;
#endif
}

bool ProcessUtils::LaunchProcess(const std::filesystem::path& exePath, const std::vector<std::string>& args,
                                  bool detached)
{
    if (exePath.empty() || !std::filesystem::exists(exePath))
    {
        PLOG_ERROR << "Invalid executable path: " << exePath.string();
        return false;
    }

#ifdef _WIN32
    std::string cmdLine = "\"" + exePath.string() + "\"";
    for (const auto& arg : args)
    {
        cmdLine += " \"" + arg + "\"";
    }

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;

    DWORD creationFlags = detached ? DETACHED_PROCESS : 0;

    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()), nullptr, nullptr, FALSE, creationFlags, nullptr,
                        nullptr, &si, &pi))
    {
        PLOG_ERROR << "CreateProcessA failed: " << GetLastError();
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    PLOG_INFO << "Launched process: " << exePath.string();
    return true;
#else
    pid_t pid = fork();
    if (pid < 0)
    {
        PLOG_ERROR << "fork() failed: " << strerror(errno);
        return false;
    }

    if (pid == 0)
    {
        if (detached)
        {
            if (setsid() < 0)
            {
                PLOG_ERROR << "setsid() failed: " << strerror(errno);
                _exit(1);
            }
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exePath.c_str()));
        for (const auto& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(exePath.c_str(), argv.data());

        PLOG_ERROR << "execv() failed: " << strerror(errno);
        _exit(1);
    }

    if (!detached)
    {
        int status;
        waitpid(pid, &status, 0);
    }

    PLOG_INFO << "Launched process: " << exePath;
    return true;
#endif
}

bool ProcessUtils::LaunchProcessWithStdin(const std::filesystem::path& exePath, const std::vector<std::string>& args,
                                          const std::string& stdinContent, bool detached)
{
    if (exePath.empty() || !std::filesystem::exists(exePath))
    {
        PLOG_ERROR << "Invalid executable path: " << exePath.string();
        return false;
    }

#ifdef _WIN32
    // Create pipe for stdin
    HANDLE hStdinRead = nullptr;
    HANDLE hStdinWrite = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0))
    {
        PLOG_ERROR << "CreatePipe failed: " << GetLastError();
        return false;
    }

    // Ensure the write handle is not inherited
    if (!SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0))
    {
        PLOG_ERROR << "SetHandleInformation failed: " << GetLastError();
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        return false;
    }

    // Build command line
    std::string cmdLine = "\"" + exePath.string() + "\"";
    for (const auto& arg : args)
    {
        cmdLine += " \"" + arg + "\"";
    }

    // Configure process startup info with stdin redirection
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = hStdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

    DWORD creationFlags = CREATE_NO_WINDOW;
    if (detached)
    {
        creationFlags |= DETACHED_PROCESS;
    }

    // Create the process
    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()), nullptr, nullptr, TRUE, creationFlags, nullptr,
                        nullptr, &si, &pi))
    {
        PLOG_ERROR << "CreateProcessA failed: " << GetLastError();
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        return false;
    }

    // Close the read end of the pipe in parent process
    CloseHandle(hStdinRead);

    // Write stdin content to the pipe
    DWORD bytesWritten = 0;
    const char* data = stdinContent.c_str();
    DWORD dataSize = static_cast<DWORD>(stdinContent.size());

    if (!WriteFile(hStdinWrite, data, dataSize, &bytesWritten, nullptr))
    {
        PLOG_ERROR << "WriteFile failed: " << GetLastError();
        CloseHandle(hStdinWrite);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    // Close write handle to signal EOF to child process
    CloseHandle(hStdinWrite);

    // Close process handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    PLOG_INFO << "Launched process with stdin: " << exePath.string() << " (" << dataSize << " bytes written)";
    return true;
#else
    // Linux implementation using fork/exec with pipe
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        PLOG_ERROR << "pipe() failed: " << strerror(errno);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        PLOG_ERROR << "fork() failed: " << strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0)
    {
        // Child process
        close(pipefd[1]); // Close write end

        // Redirect stdin to pipe
        if (dup2(pipefd[0], STDIN_FILENO) == -1)
        {
            PLOG_ERROR << "dup2() failed: " << strerror(errno);
            _exit(1);
        }
        close(pipefd[0]);

        if (detached)
        {
            if (setsid() < 0)
            {
                PLOG_ERROR << "setsid() failed: " << strerror(errno);
                _exit(1);
            }
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exePath.c_str()));
        for (const auto& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(exePath.c_str(), argv.data());

        PLOG_ERROR << "execv() failed: " << strerror(errno);
        _exit(1);
    }

    // Parent process
    close(pipefd[0]); // Close read end

    // Write stdin content to pipe
    const char* data = stdinContent.c_str();
    size_t dataSize = stdinContent.size();
    ssize_t bytesWritten = write(pipefd[1], data, dataSize);

    if (bytesWritten < 0)
    {
        PLOG_ERROR << "write() failed: " << strerror(errno);
        close(pipefd[1]);
        return false;
    }

    // Close write end to signal EOF
    close(pipefd[1]);

    if (!detached)
    {
        int status;
        waitpid(pid, &status, 0);
    }

    PLOG_INFO << "Launched process with stdin: " << exePath << " (" << dataSize << " bytes written)";
    return true;
#endif
}

} // namespace utils
