#include "UpdateApplier.hpp"
#include "../platform/ProcessUtils.hpp"

#include <plog/Log.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace updater
{

// Helper function to replace all occurrences of a substring
std::string replaceAll(std::string str, const std::string& from, const std::string& to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

std::string generateUpdateScript(const std::string& appDir, const std::string& packagePath,
                                                        const std::string& exeName, const std::string& manifestPath)
{
    // Convert paths to absolute and normalize
    fs::path absAppDir = fs::absolute(appDir);
    fs::path absPackagePath = fs::absolute(packagePath);
    fs::path absManifestPath = fs::absolute(manifestPath);
    fs::path backupDir = absAppDir / "backup";
    fs::path updateTempDir = absAppDir / "update-temp";
    fs::path exePath = absAppDir / exeName;
    fs::path logsDir = absAppDir / "logs";
    fs::path logFile = logsDir / "update.log";

    // Use raw string literal with named placeholders
    std::string scriptTemplate = R"(@echo off
setlocal enabledelayedexpansion

set "APP_DIR=__APP_DIR__"
set "PACKAGE_PATH=__PACKAGE_PATH__"
set "MANIFEST_PATH=__MANIFEST_PATH__"
set "BACKUP_DIR=__BACKUP_DIR__"
set "UPDATE_TEMP_DIR=__UPDATE_TEMP_DIR__"
set "EXE_PATH=__EXE_PATH__"
set "LOG_FILE=__LOG_FILE__"

if not exist "%APP_DIR%\logs" mkdir "%APP_DIR%\logs"
echo DQX Utility Update Script > "%LOG_FILE%"
echo ================================== >> "%LOG_FILE%"
echo Started: %date% %time% >> "%LOG_FILE%"
echo. >> "%LOG_FILE%"

echo Waiting for application to exit... >> "%LOG_FILE%"
set WAIT_COUNT=0
:WAIT_LOOP
:: Check if process is still running
tasklist /FI "IMAGENAME eq __EXE_NAME__" /NH 2>NUL | find /I "__EXE_NAME__" >NUL 2>&1
if errorlevel 1 (
    :: Process not found - app has exited
    echo Application exited successfully >> "%LOG_FILE%"
    goto CONTINUE_UPDATE
)
:: Process still running - wait
if %WAIT_COUNT% GEQ 15 (
    echo ERROR: Timeout waiting for application to exit >> "%LOG_FILE%"
    goto ERROR_EXIT
)
echo Waiting... attempt %WAIT_COUNT% >> "%LOG_FILE%"
timeout /t 1 /nobreak >NUL
set /a WAIT_COUNT+=1
goto WAIT_LOOP

:CONTINUE_UPDATE
echo. >> "%LOG_FILE%"

cd /d "%APP_DIR%" >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo ERROR: Failed to change to app directory >> "%LOG_FILE%"
    goto ERROR_EXIT
)

echo Creating backup... >> "%LOG_FILE%"
if exist "%BACKUP_DIR%" (
    echo Removing old backup... >> "%LOG_FILE%"
    rmdir /s /q "%BACKUP_DIR%" >> "%LOG_FILE%" 2>&1
)
mkdir "%BACKUP_DIR%" >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo ERROR: Failed to create backup directory >> "%LOG_FILE%"
    goto ERROR_EXIT
)

echo Backing up files... >> "%LOG_FILE%"
if exist "%EXE_PATH%" (
    copy "%EXE_PATH%" "%BACKUP_DIR%\" >> "%LOG_FILE%" 2>&1
)
for %%f in ("%APP_DIR%\*.dll") do (
    copy "%%f" "%BACKUP_DIR%\" >> "%LOG_FILE%" 2>&1
)
if exist "%APP_DIR%\assets" (
    xcopy "%APP_DIR%\assets" "%BACKUP_DIR%\assets\" /E /I /Y >> "%LOG_FILE%" 2>&1
)
if exist "%MANIFEST_PATH%" (
    copy "%MANIFEST_PATH%" "%BACKUP_DIR%\" >> "%LOG_FILE%" 2>&1
)
echo Backup completed >> "%LOG_FILE%"
echo. >> "%LOG_FILE%"

echo Extracting update package to temp directory... >> "%LOG_FILE%"
set "EXTRACT_TEMP=%UPDATE_TEMP_DIR%\extracted"
if exist "%EXTRACT_TEMP%" rmdir /s /q "%EXTRACT_TEMP%" >> "%LOG_FILE%" 2>&1
mkdir "%EXTRACT_TEMP%" >> "%LOG_FILE%" 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -Command "& { try { Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::ExtractToDirectory('%PACKAGE_PATH%', '%EXTRACT_TEMP%'); exit 0 } catch { Write-Error $_.Exception.Message; exit 1 } }" >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo ERROR: Failed to extract update package >> "%LOG_FILE%"
    goto RESTORE_BACKUP
)
echo Package extracted successfully >> "%LOG_FILE%"
echo. >> "%LOG_FILE%"

echo Copying new files (preserving config.toml)... >> "%LOG_FILE%"
:: Copy executable
if exist "%EXTRACT_TEMP%\__EXE_NAME__" (
    copy /y "%EXTRACT_TEMP%\__EXE_NAME__" "%APP_DIR%\" >> "%LOG_FILE%" 2>&1
)
:: Copy DLLs
for %%f in ("%EXTRACT_TEMP%\*.dll") do (
    copy /y "%%f" "%APP_DIR%\" >> "%LOG_FILE%" 2>&1
)
:: Copy assets folder
if exist "%EXTRACT_TEMP%\assets" (
    xcopy "%EXTRACT_TEMP%\assets" "%APP_DIR%\assets\" /E /I /Y >> "%LOG_FILE%" 2>&1
)
:: Copy manifest but NOT config.toml
if exist "%EXTRACT_TEMP%\manifest.json" (
    copy /y "%EXTRACT_TEMP%\manifest.json" "%APP_DIR%\" >> "%LOG_FILE%" 2>&1
)
echo Files copied successfully >> "%LOG_FILE%"
echo. >> "%LOG_FILE%"

echo Update completed successfully! >> "%LOG_FILE%"
echo Cleaning up... >> "%LOG_FILE%"

if exist "%PACKAGE_PATH%" (
    del /f /q "%PACKAGE_PATH%" >> "%LOG_FILE%" 2>&1
)

if exist "%BACKUP_DIR%" (
    echo Removing backup directory... >> "%LOG_FILE%"
    rmdir /s /q "%BACKUP_DIR%" >> "%LOG_FILE%" 2>&1
)

echo Completed: %date% %time% >> "%LOG_FILE%"
echo ================================== >> "%LOG_FILE%"

echo Restarting application...
timeout /t 1 /nobreak >NUL
start "" "%EXE_PATH%"

echo Cleaning up update files...
:: Self-delete: spawn background process to clean up update-temp directory after a short delay
start /b cmd /c "timeout /t 2 /nobreak >nul & rmdir /s /q "%UPDATE_TEMP_DIR%" 2>nul"
exit /b 0

:RESTORE_BACKUP
echo ================================== >> "%LOG_FILE%"
echo Restoring from backup... >> "%LOG_FILE%"
if not exist "%BACKUP_DIR%" (
    echo ERROR: Backup directory not found! >> "%LOG_FILE%"
    goto ERROR_EXIT
)

xcopy "%BACKUP_DIR%\*" "%APP_DIR%\" /E /I /Y >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
    echo ERROR: Failed to restore backup >> "%LOG_FILE%"
    goto ERROR_EXIT
)
echo Backup restored successfully >> "%LOG_FILE%"

if exist "%BACKUP_DIR%" (
    rmdir /s /q "%BACKUP_DIR%" >> "%LOG_FILE%" 2>&1
)
echo Restoration completed: %date% %time% >> "%LOG_FILE%"
echo ================================== >> "%LOG_FILE%"

msg * "Update failed. The application has been restored from backup. Check update.log for details."
start "" "%EXE_PATH%"
exit /b 1

:ERROR_EXIT
echo ================================== >> "%LOG_FILE%"
echo Update failed: %date% %time% >> "%LOG_FILE%"
echo ================================== >> "%LOG_FILE%"
msg * "Update failed. Check update.log for details."
exit /b 1
)";

    // Replace placeholders with actual values using safe string replacement
    scriptTemplate = replaceAll(scriptTemplate, "__APP_DIR__", absAppDir.string());
    scriptTemplate = replaceAll(scriptTemplate, "__PACKAGE_PATH__", absPackagePath.string());
    scriptTemplate = replaceAll(scriptTemplate, "__MANIFEST_PATH__", absManifestPath.string());
    scriptTemplate = replaceAll(scriptTemplate, "__BACKUP_DIR__", backupDir.string());
    scriptTemplate = replaceAll(scriptTemplate, "__UPDATE_TEMP_DIR__", updateTempDir.string());
    scriptTemplate = replaceAll(scriptTemplate, "__EXE_PATH__", exePath.string());
    scriptTemplate = replaceAll(scriptTemplate, "__LOG_FILE__", logFile.string());
    scriptTemplate = replaceAll(scriptTemplate, "__EXE_NAME__", exeName);

    return scriptTemplate;
}

} // namespace updater

namespace updater
{

UpdateApplier::UpdateApplier(const std::string& appDir)
    : appDir_(appDir)
{
}

bool UpdateApplier::applyUpdate(const std::string& packagePath, const std::string& configTemplatePath,
                                ApplyCallback callback, std::string& outError)
{
    // Get executable path and name
    auto exePath = utils::ProcessUtils::GetExecutablePath();
    if (exePath.empty())
    {
        outError = "Failed to get executable path";
        PLOG_ERROR << outError;
        if (callback)
            callback(false, outError);
        return false;
    }

    std::string exeName = exePath.filename().string();
    fs::path manifestPath = fs::path(appDir_) / "manifest.json";

    // Generate update script
    std::string scriptContent = generateUpdateScript(
        appDir_, packagePath, exeName, manifestPath.string());

    PLOG_INFO << "Update script generated";
    PLOG_INFO << "Script size: " << scriptContent.size() << " bytes";
    PLOG_INFO << "Update log will be written to: " << (fs::path(appDir_) / "logs" / "update.log").string();

    // Write script to temporary batch file
    fs::path updateTempDir = fs::path(appDir_) / "update-temp";
    fs::path batchFilePath = updateTempDir / "apply_update.bat";

    // Ensure update-temp directory exists
    std::error_code ec;
    if (!fs::exists(updateTempDir, ec))
    {
        if (!fs::create_directories(updateTempDir, ec))
        {
            outError = "Failed to create update-temp directory: " + ec.message();
            PLOG_ERROR << outError;
            if (callback)
                callback(false, outError);
            return false;
        }
    }

    // Write batch script to file
    std::ofstream batchFile(batchFilePath, std::ios::out | std::ios::trunc);
    if (!batchFile.is_open())
    {
        outError = "Failed to create batch file: " + batchFilePath.string();
        PLOG_ERROR << outError;
        if (callback)
            callback(false, outError);
        return false;
    }

    batchFile << scriptContent;
    batchFile.close();

    if (batchFile.fail())
    {
        outError = "Failed to write batch script to file";
        PLOG_ERROR << outError;
        if (callback)
            callback(false, outError);
        return false;
    }

    PLOG_INFO << "Update script written to: " << batchFilePath.string();

    // Launch the batch file with a console (required for batch commands to work)
    PLOG_INFO << "Launching update script...";
    if (!utils::ProcessUtils::LaunchProcess(batchFilePath, {}, false))
    {
        outError = "Failed to launch update script";
        PLOG_ERROR << outError;
        if (callback)
            callback(false, outError);
        return false;
    }

    PLOG_INFO << "Update script launched successfully, application will exit gracefully";

    if (callback)
    {
        callback(true, "Update process started");
    }

    return true;
}

} // namespace updater
