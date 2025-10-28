#include "BackupManager.hpp"

#include <plog/Log.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace updater
{

BackupManager::BackupManager(const std::string& appDir) : appDir_(appDir), backupDir_(appDir + "/backup")
{
}

bool BackupManager::createBackup(std::string& outError)
{
    try
    {
        if (fs::exists(backupDir_))
        {
            PLOG_INFO << "Removing old backup";
            fs::remove_all(backupDir_);
        }

        fs::create_directories(backupDir_);

        std::vector<std::string> filesToBackup = {"dqx-utility.exe", "SDL3.dll"};

        for (const auto& file : filesToBackup)
        {
            fs::path sourcePath = fs::path(appDir_) / file;
            fs::path destPath = fs::path(backupDir_) / file;

            if (fs::exists(sourcePath))
            {
                fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
                PLOG_DEBUG << "Backed up: " << file;
            }
        }

        fs::path assetsSource = fs::path(appDir_) / "assets";
        fs::path assetsDest = fs::path(backupDir_) / "assets";

        if (fs::exists(assetsSource))
        {
            fs::copy(assetsSource, assetsDest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            PLOG_DEBUG << "Backed up: assets/";
        }

        PLOG_INFO << "Backup created successfully: " << backupDir_;
        return true;
    }
    catch (const fs::filesystem_error& e)
    {
        outError = std::string("Filesystem error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Backup error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool BackupManager::restoreFromBackup(std::string& outError)
{
    try
    {
        if (!fs::exists(backupDir_))
        {
            outError = "Backup directory does not exist";
            PLOG_ERROR << outError;
            return false;
        }

        PLOG_INFO << "Restoring from backup: " << backupDir_;

        for (const auto& entry : fs::directory_iterator(backupDir_))
        {
            fs::path destPath = fs::path(appDir_) / entry.path().filename();

            if (entry.is_directory())
            {
                if (fs::exists(destPath))
                {
                    fs::remove_all(destPath);
                }
                fs::copy(entry.path(), destPath, fs::copy_options::recursive);
            }
            else
            {
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
            }

            PLOG_DEBUG << "Restored: " << entry.path().filename().string();
        }

        PLOG_INFO << "Restore completed successfully";
        return true;
    }
    catch (const fs::filesystem_error& e)
    {
        outError = std::string("Filesystem error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Restore error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool BackupManager::hasBackup() const
{
    return fs::exists(backupDir_) && !fs::is_empty(backupDir_);
}

void BackupManager::cleanupBackup()
{
    try
    {
        if (fs::exists(backupDir_))
        {
            fs::remove_all(backupDir_);
            PLOG_INFO << "Backup cleaned up: " << backupDir_;
        }
    }
    catch (const std::exception& e)
    {
        PLOG_WARNING << "Failed to cleanup backup: " << e.what();
    }
}

bool BackupManager::copyDirectory(const std::string& source, const std::string& dest, std::string& outError)
{
    try
    {
        fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    }
    catch (const fs::filesystem_error& e)
    {
        outError = std::string("Failed to copy directory: ") + e.what();
        return false;
    }
}

bool BackupManager::deleteDirectory(const std::string& path)
{
    try
    {
        if (fs::exists(path))
        {
            fs::remove_all(path);
        }
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

} // namespace updater
