#pragma once

#include "UpdateTypes.hpp"
#include "Version.hpp"

#include <functional>
#include <memory>

namespace updater
{

// Callback types
using UpdateCheckCallback = std::function<void(bool updateAvailable)>;
using DownloadProgressCallback = std::function<void(const DownloadProgress&)>;
using UpdateCompleteCallback = std::function<void(bool success, const std::string& message)>;

// Main updater service
class UpdaterService
{
public:
    UpdaterService();
    ~UpdaterService();

    // Disable copy
    UpdaterService(const UpdaterService&) = delete;
    UpdaterService& operator=(const UpdaterService&) = delete;

    // Initialize updater service
    void initialize(const std::string& githubOwner, const std::string& githubRepo, const Version& currentVersion);

    // Shutdown updater service (cancel pending operations)
    void shutdown();

    // Check for updates (async, non-blocking)
    void checkForUpdatesAsync(UpdateCheckCallback callback = nullptr);

    // Start download (async, non-blocking)
    void startDownload(DownloadProgressCallback progressCallback = nullptr);

    // Cancel ongoing download
    void cancelDownload();

    // Apply downloaded update (will exit application)
    void applyUpdate(UpdateCompleteCallback callback = nullptr);

    // State queries (thread-safe)
    UpdateState getState() const;
    UpdateInfo getUpdateInfo() const;
    DownloadProgress getDownloadProgress() const;
    UpdateError getLastError() const;

    // Check if update is available
    bool isUpdateAvailable() const;

    // Check if service is initialized (returns false in dev mode)
    bool isInitialized() const;

private:
    // Check if running in packaged build (has .dqxu_packaged marker)
    bool isPackagedBuild() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace updater

updater::UpdaterService* UpdaterService_Get();
void UpdaterService_Set(updater::UpdaterService* service);
