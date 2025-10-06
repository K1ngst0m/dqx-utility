#include "CrashHandler.hpp"
#include <plog/Log.h>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <ctime>

static thread_local const char* g_current_operation = nullptr;

static LONG WINAPI CrashHandlerFunction(EXCEPTION_POINTERS* ex)
{
    PLOG_FATAL << "=== APPLICATION CRASHED ===";
    PLOG_FATAL << "Exception code: 0x" << std::hex << ex->ExceptionRecord->ExceptionCode;
    PLOG_FATAL << "Exception address: 0x" << std::hex << ex->ExceptionRecord->ExceptionAddress;
    
    if (g_current_operation)
    {
        PLOG_FATAL << "Operation: " << g_current_operation;
    }
    
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
        
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                         MiniDumpNormal, &mdei, NULL, NULL);
        
        CloseHandle(file);
        PLOG_FATAL << "Crash dump written to: " << filename;
    }
    
    PLOG_FATAL << "Check logs/run.log and " << filename << " for details";
    
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

void utils::CrashHandler::Initialize()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashHandlerFunction);
    PLOG_INFO << "Crash handler installed";
#endif
}

void utils::CrashHandler::SetContext(const char* operation)
{
#ifdef _WIN32
    g_current_operation = operation;
#endif
}
