#include "CrashHandler.hpp"
#include <plog/Log.h>
#include <cpptrace/cpptrace.hpp>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <exception>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <filesystem>
#include <ctime>
#include <iostream>
#include <cstdlib>

static std::atomic<std::atomic<bool>*> g_fatal_flag{ nullptr };
static std::atomic<void (*)()> g_fatal_cleanup{ nullptr };
static std::terminate_handler g_prev_terminate = nullptr;

static thread_local const char* g_current_operation = nullptr;

static void NotifyFatalObserver()
{
    if (auto* flag = g_fatal_flag.load(std::memory_order_acquire))
    {
        flag->store(true, std::memory_order_relaxed);
    }
}

static void CrashTerminateHandler()
{
    NotifyFatalObserver();
    if (auto fn = g_fatal_cleanup.load(std::memory_order_acquire))
    {
        try
        {
            fn();
        }
        catch (...)
        {
        }
    }
    if (g_prev_terminate)
    {
        g_prev_terminate();
        return;
    }
    std::abort();
}

static const char* ExceptionCodeToString(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
        return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
        return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:
        return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:
        return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    default:
        return "UNKNOWN_EXCEPTION";
    }
}

static LONG WINAPI CrashHandlerFunction(EXCEPTION_POINTERS* ex)
{
    NotifyFatalObserver();
    if (auto fn = g_fatal_cleanup.load(std::memory_order_acquire))
    {
        try
        {
            fn();
        }
        catch (...)
        {
        }
    }
    // Basic header
    PLOG_FATAL << "=== APPLICATION CRASHED ===";
    DWORD code = ex->ExceptionRecord->ExceptionCode;
    PLOG_FATAL << "Exception: 0x" << std::hex << code << " (" << ExceptionCodeToString(code) << ")";
    PLOG_FATAL << "Address: 0x" << std::hex << ex->ExceptionRecord->ExceptionAddress;
    if (g_current_operation)
    {
        PLOG_FATAL << "Operation: " << g_current_operation;
    }

    try
    {
        PLOG_FATAL << "Stack trace (most recent call first):";

        std::ostringstream trace_ss;
        try
        {
            trace_ss << cpptrace::generate_trace();
        }
        catch (...)
        {
        }

        std::string trace = trace_ss.str();

        auto trim_path = [](const std::string& filename) -> std::string
        {
            // Prefer to show path starting after /src/ or \src\, otherwise show basename.
            size_t pos_back = filename.find("\\src\\");
            size_t pos_slash = filename.find("/src/");
            size_t pos_src = std::string::npos;
            if (pos_back != std::string::npos)
                pos_src = pos_back;
            else if (pos_slash != std::string::npos)
                pos_src = pos_slash;

            if (pos_src != std::string::npos)
            {
                std::string s = filename.substr(pos_src + 5); // skip "\src\" or "/src/"
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            }

            size_t p = filename.find_last_of("\\/");
            if (p != std::string::npos)
            {
                std::string s = filename.substr(p + 1);
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            }

            std::string s = filename;
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };

        if (trace.empty())
        {
            PLOG_FATAL << "No stack trace available (cpptrace returned empty).";
        }
        else
        {
            std::istringstream iss(trace);
            std::string line;
            int idx = 0;

            while (std::getline(iss, line))
            {
                // Expected typical forms:
                //  - "function at /path/to/src/file.cpp:123"
                //  - "function ( /path/to/src/file.cpp:123 )"
                // We'll try to be tolerant and extract function, file and line when possible.

                std::string func = line;
                std::string fileline;

                size_t atPos = line.find(" at ");
                if (atPos != std::string::npos)
                {
                    func = line.substr(0, atPos);
                    fileline = line.substr(atPos + 4);
                }
                else
                {
                    size_t paren = line.rfind('(');
                    size_t parenClose = line.rfind(')');
                    if (paren != std::string::npos && parenClose != std::string::npos && paren < parenClose)
                    {
                        func = line.substr(0, paren);
                        fileline = line.substr(paren + 1, parenClose - paren - 1);
                    }
                }

                // Trim whitespace helpers
                auto trim_ws = [](std::string& s)
                {
                    size_t l = 0;
                    while (l < s.size() && std::isspace(static_cast<unsigned char>(s[l])))
                        ++l;
                    size_t r = s.size();
                    while (r > l && std::isspace(static_cast<unsigned char>(s[r - 1])))
                        --r;
                    s = s.substr(l, r - l);
                };

                trim_ws(func);
                trim_ws(fileline);

                std::string short_file;
                std::string lineNum;

                if (!fileline.empty())
                {
                    // Find last colon that looks like the separator before line number.
                    size_t colon = fileline.find_last_of(':');
                    if (colon != std::string::npos && colon + 1 < fileline.size() &&
                        std::isdigit(static_cast<unsigned char>(fileline[colon + 1])))
                    {
                        lineNum = fileline.substr(colon + 1);
                        short_file = fileline.substr(0, colon);
                    }
                    else
                    {
                        short_file = fileline;
                    }

                    short_file = trim_path(short_file);
                }

                std::ostringstream entry;
                entry << "#" << idx << " " << func;
                if (!short_file.empty())
                {
                    entry << " at " << short_file;
                    if (!lineNum.empty())
                    {
                        entry << ":" << lineNum;
                    }
                }

                PLOG_FATAL << entry.str();
                ++idx;
            } // while getline

        } // else trace not empty
    }
    catch (const std::exception& e)
    {
        PLOG_FATAL << "Failed to generate stack trace: " << e.what();
    }
    catch (...)
    {
        PLOG_FATAL << "Failed to generate stack trace: unknown error";
    }

    // Keep existing minidump generation
    char filename[256];
    std::time_t now = std::time(nullptr);
    std::tm tm_buf;
    localtime_s(&tm_buf, &now);
    std::strftime(filename, sizeof(filename), "logs/crash_%Y%m%d_%H%M%S.dmp", &tm_buf);

    HANDLE file = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = ex;
        mdei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &mdei, NULL, NULL);

        CloseHandle(file);
        PLOG_FATAL << "Dump: " << filename;
    }
    else
    {
        PLOG_FATAL << "Failed to write crash dump: " << GetLastError();
    }

    PLOG_FATAL << "Check logs/run.log and " << filename << " for details";

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void utils::CrashHandler::Initialize()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashHandlerFunction);
    g_prev_terminate = std::set_terminate(CrashTerminateHandler);
    PLOG_INFO << "Crash handler installed";
#endif
}

void utils::CrashHandler::SetContext(const char* operation)
{
#ifdef _WIN32
    g_current_operation = operation;
#endif
}

void utils::CrashHandler::RegisterFatalFlag(std::atomic<bool>* flag)
{
#ifdef _WIN32
    g_fatal_flag.store(flag, std::memory_order_release);
#endif
}

void utils::CrashHandler::RegisterFatalCleanup(void (*fn)())
{
#ifdef _WIN32
    g_fatal_cleanup.store(fn, std::memory_order_release);
#endif
}
