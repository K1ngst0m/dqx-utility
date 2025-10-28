#pragma once

#include <string>
#include <vector>

namespace updater
{

class BackupManager
{
public:
    BackupManager(const std::string& appDir);
    ~BackupManager() = default;

    bool createBackup(std::string& outError);
    bool restoreFromBackup(std::string& outError);
    bool hasBackup() const;
    void cleanupBackup();

    std::string getBackupDir() const { return backupDir_; }

private:
    std::string appDir_;
    std::string backupDir_;

    bool copyDirectory(const std::string& source, const std::string& dest, std::string& outError);
    bool deleteDirectory(const std::string& path);
};

} // namespace updater
