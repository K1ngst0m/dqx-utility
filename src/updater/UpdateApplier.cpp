#include "UpdateApplier.hpp"

#include <plog/Log.h>

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace updater
{

UpdateApplier::UpdateApplier(const std::string& appDir) : appDir_(appDir)
{
}

bool UpdateApplier::applyUpdate(const std::string& packagePath, const std::string& configTemplatePath,
                               ApplyCallback callback, std::string& outError)
{
    std::string scriptPath;
    if (!generateBatchScript(packagePath, configTemplatePath, scriptPath, outError))
    {
        if (callback)
        {
            callback(false, outError);
        }
        return false;
    }

    if (!launchBatchScript(scriptPath, outError))
    {
        if (callback)
        {
            callback(false, outError);
        }
        return false;
    }

    PLOG_INFO << "Update script launched, application will exit";
    return true;
}

bool UpdateApplier::generateBatchScript(const std::string& packagePath, const std::string& configTemplatePath,
                                       std::string& outScriptPath, std::string& outError)
{
    try
    {
        outScriptPath = appDir_ + "/apply_update.bat";

        std::ofstream script(outScriptPath);
        if (!script.is_open())
        {
            outError = "Failed to create batch script";
            return false;
        }

        script << "@echo off\n";
        script << "echo DQX Utility Updater\n";
        script << "echo Waiting for application to exit...\n";
        script << "timeout /t 2 /nobreak >nul\n\n";

        script << "echo Creating backup...\n";
        script << "if exist backup rmdir /s /q backup\n";
        script << "mkdir backup\n";
        script << "if exist dqx-utility.exe copy /y dqx-utility.exe backup\\\n";
        script << "if exist SDL3.dll copy /y SDL3.dll backup\\\n";
        script << "if exist assets xcopy /e /i /y assets backup\\assets\n\n";

        script << "echo Extracting update...\n";
        script << "tar -xf \"" << packagePath << "\" --exclude=config.toml\n";
        script << "if errorlevel 1 (\n";
        script << "    echo Update extraction failed, restoring backup...\n";
        script << "    if exist backup\\dqx-utility.exe copy /y backup\\dqx-utility.exe .\n";
        script << "    if exist backup\\SDL3.dll copy /y backup\\SDL3.dll .\n";
        script << "    if exist backup\\assets xcopy /e /i /y backup\\assets assets\n";
        script << "    echo Update failed!\n";
        script << "    pause\n";
        script << "    exit /b 1\n";
        script << ")\n\n";

        script << "echo Merging config...\n";
        script << "if exist config.toml (\n";
        script << "    copy /y config.toml config.toml.backup\n";
        script << ")\n\n";

        script << "echo Update completed successfully!\n";
        script << "echo Restarting application...\n";
        script << "timeout /t 1 /nobreak >nul\n";
        script << "start \"\" dqx-utility.exe\n\n";

        script << "del \"" << packagePath << "\"\n";
        script << "del \"%~f0\"\n";

        script.close();

        PLOG_INFO << "Batch script generated: " << outScriptPath;
        return true;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Failed to generate batch script: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool UpdateApplier::launchBatchScript(const std::string& scriptPath, std::string& outError)
{
#ifdef _WIN32
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};

    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::string cmdLine = "cmd.exe /c \"" + scriptPath + "\"";

    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, appDir_.c_str(), &si, &pi))
    {
        outError = "Failed to launch update script";
        PLOG_ERROR << outError;
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    PLOG_INFO << "Update script launched successfully";
    return true;
#else
    outError = "Update script launching not implemented for this platform";
    PLOG_ERROR << outError;
    return false;
#endif
}

} // namespace updater
