#include "UpdaterService.hpp"
#include "GitHubReleaseChecker.hpp"
#include "PackageDownloader.hpp"
#include "UpdateApplier.hpp"

#include <plog/Log.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace updater
{

struct UpdaterService::Impl
{
    // Configuration
    std::string githubOwner;
    std::string githubRepo;
    Version currentVersion;

    // State (atomic for thread-safety)
    std::atomic<UpdateState> state{ UpdateState::Idle };

    // Update information
    mutable std::mutex infoMutex;
    UpdateInfo updateInfo;
    DownloadProgress downloadProgress;
    UpdateError lastError;

    // GitHub release checker
    std::unique_ptr<GitHubReleaseChecker> releaseChecker;

    // Package downloader
    std::unique_ptr<PackageDownloader> downloader;

    // Update applier
    std::unique_ptr<UpdateApplier> applier;

    // Downloaded package path
    std::string downloadedPackagePath;
    std::string appDirectory;

    // Background threads
    std::thread checkThread;
    std::thread downloadThread;

    // Callbacks
    UpdateCheckCallback checkCallback;
    DownloadProgressCallback progressCallback;
    UpdateCompleteCallback completeCallback;

    // Initialized flag
    bool initialized = false;

    Impl() = default;

    ~Impl()
    {
        // Ensure threads are joined
        if (checkThread.joinable())
        {
            checkThread.join();
        }
        if (downloadThread.joinable())
        {
            downloadThread.join();
        }
    }
};

UpdaterService::UpdaterService()
    : impl_(std::make_unique<Impl>())
{
}

UpdaterService::~UpdaterService() = default;

void UpdaterService::initialize(const std::string& githubOwner, const std::string& githubRepo,
                                const Version& currentVersion)
{
    if (!isPackagedBuild())
    {
        PLOG_INFO << "UpdaterService disabled: Running in development mode (manifest.json is_release=false or missing)";
        PLOG_INFO << "To enable updater, build a packaged release using CMake package targets";
        return;
    }

    impl_->githubOwner = githubOwner;
    impl_->githubRepo = githubRepo;
    impl_->currentVersion = currentVersion;

    impl_->appDirectory = std::filesystem::current_path().string();

    impl_->releaseChecker = std::make_unique<GitHubReleaseChecker>(githubOwner, githubRepo);
    impl_->downloader = std::make_unique<PackageDownloader>();
    impl_->applier = std::make_unique<UpdateApplier>(impl_->appDirectory);

    impl_->initialized = true;

    PLOG_INFO << "UpdaterService initialized for " << githubOwner << "/" << githubRepo
              << " (current version: " << currentVersion.toString() << ")";
}

void UpdaterService::shutdown()
{
    if (!impl_->initialized)
    {
        return;
    }

    PLOG_INFO << "UpdaterService shutting down";

    // Cancel any pending operations
    cancelDownload();

    // Wait for threads to finish
    if (impl_->checkThread.joinable())
    {
        impl_->checkThread.join();
    }
    if (impl_->downloadThread.joinable())
    {
        impl_->downloadThread.join();
    }

    impl_->initialized = false;
}

void UpdaterService::checkForUpdatesAsync(UpdateCheckCallback callback)
{
    if (!impl_->initialized)
    {
        PLOG_ERROR << "UpdaterService not initialized";
        if (callback)
        {
            callback(false);
        }
        return;
    }

    if (impl_->state != UpdateState::Idle)
    {
        PLOG_WARNING << "Update check already in progress or update available";
        return;
    }

    impl_->checkCallback = callback;
    impl_->state = UpdateState::Checking;

    // Use GitHubReleaseChecker to check for updates
    impl_->releaseChecker->checkLatestReleaseAsync(
        impl_->currentVersion,
        [this](bool success, const UpdateInfo& info, const std::string& error)
        {
            if (success)
            {
                // Update available
                std::lock_guard<std::mutex> lock(impl_->infoMutex);
                impl_->updateInfo = info;
                impl_->state = UpdateState::Available;

                PLOG_INFO << "Update available: " << info.version;

                if (impl_->checkCallback)
                {
                    impl_->checkCallback(true);
                }
            }
            else
            {
                // No update or error
                if (!error.empty())
                {
                    std::lock_guard<std::mutex> lock(impl_->infoMutex);
                    impl_->lastError = UpdateError(error);
                    PLOG_DEBUG << "Update check: " << error;
                }

                impl_->state = UpdateState::Idle;

                if (impl_->checkCallback)
                {
                    impl_->checkCallback(false);
                }
            }
        });
}

void UpdaterService::startDownload(DownloadProgressCallback progressCallback)
{
    if (!impl_->initialized)
    {
        PLOG_ERROR << "UpdaterService not initialized";
        return;
    }

    if (impl_->state != UpdateState::Available)
    {
        PLOG_WARNING << "No update available to download";
        return;
    }

    impl_->progressCallback = progressCallback;
    impl_->state = UpdateState::Downloading;

    std::string downloadUrl;
    {
        std::lock_guard<std::mutex> lock(impl_->infoMutex);
        downloadUrl = impl_->updateInfo.downloadUrl;
    }

    std::string tempDir = impl_->appDirectory + "/update-temp";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        PLOG_ERROR << "Failed to create update-temp directory: " << ec.message();
        return;
    }
    
    std::string tempPath = tempDir + "/update-package.zip";

    impl_->downloader->downloadAsync(
        downloadUrl, tempPath,
        [this](const DownloadProgress& progress)
        {
            std::lock_guard<std::mutex> lock(impl_->infoMutex);
            impl_->downloadProgress = progress;

            if (impl_->progressCallback)
            {
                impl_->progressCallback(progress);
            }
        },
        [this, tempPath](bool success, const std::string& filePath, const std::string& error)
        {
            if (success)
            {
                PLOG_INFO << "Package downloaded: " << filePath;
                impl_->downloadedPackagePath = filePath;
                impl_->state = UpdateState::Downloaded;
            }
            else
            {
                PLOG_ERROR << "Download failed: " << error;
                std::lock_guard<std::mutex> lock(impl_->infoMutex);
                impl_->lastError = UpdateError(error);
                impl_->state = UpdateState::Failed;
            }
        });
}

void UpdaterService::cancelDownload()
{
    if (impl_->state == UpdateState::Downloading)
    {
        PLOG_INFO << "Cancelling download...";
        impl_->downloader->cancel();
        impl_->state = UpdateState::Available;
    }
}

void UpdaterService::applyUpdate(UpdateCompleteCallback callback)
{
    if (!impl_->initialized)
    {
        PLOG_ERROR << "UpdaterService not initialized";
        if (callback)
        {
            callback(false, "Updater not initialized");
        }
        return;
    }

    if (impl_->state != UpdateState::Downloaded)
    {
        PLOG_WARNING << "No update downloaded to apply";
        if (callback)
        {
            callback(false, "No update ready to apply");
        }
        return;
    }

    impl_->completeCallback = callback;
    impl_->state = UpdateState::Applying;

    PLOG_INFO << "Applying update...";

    std::string configTemplatePath = impl_->appDirectory + "/assets/templates/config.toml";
    std::string error;

    bool success = impl_->applier->applyUpdate(
        impl_->downloadedPackagePath, configTemplatePath,
        [this](bool success, const std::string& message)
        {
            if (success)
            {
                impl_->state = UpdateState::Completed;
            }
            else
            {
                impl_->state = UpdateState::Failed;
                std::lock_guard<std::mutex> lock(impl_->infoMutex);
                impl_->lastError = UpdateError(message);
            }

            if (impl_->completeCallback)
            {
                impl_->completeCallback(success, message);
            }
        },
        error);

    if (!success)
    {
        impl_->state = UpdateState::Failed;
        std::lock_guard<std::mutex> lock(impl_->infoMutex);
        impl_->lastError = UpdateError(error);

        if (impl_->completeCallback)
        {
            impl_->completeCallback(false, error);
        }
    }
}

UpdateState UpdaterService::getState() const { return impl_->state.load(); }

UpdateInfo UpdaterService::getUpdateInfo() const
{
    std::lock_guard<std::mutex> lock(impl_->infoMutex);
    return impl_->updateInfo;
}

DownloadProgress UpdaterService::getDownloadProgress() const
{
    std::lock_guard<std::mutex> lock(impl_->infoMutex);
    return impl_->downloadProgress;
}

UpdateError UpdaterService::getLastError() const
{
    std::lock_guard<std::mutex> lock(impl_->infoMutex);
    return impl_->lastError;
}

bool UpdaterService::isUpdateAvailable() const
{
    return impl_->state == UpdateState::Available || impl_->state == UpdateState::Downloaded;
}

bool UpdaterService::isInitialized() const { return impl_ && impl_->initialized; }

bool UpdaterService::isPackagedBuild() const
{
    std::filesystem::path manifestPath = std::filesystem::current_path() / "manifest.json";
    if (!std::filesystem::exists(manifestPath))
    {
        return false;
    }

    try
    {
        std::ifstream manifestFile(manifestPath);
        if (!manifestFile.is_open())
        {
            return false;
        }

        std::string manifestContent((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());

        size_t isReleasePos = manifestContent.find("\"is_release\"");
        if (isReleasePos == std::string::npos)
        {
            return false;
        }

        size_t colonPos = manifestContent.find(':', isReleasePos);
        if (colonPos == std::string::npos)
        {
            return false;
        }

        size_t valueStart = manifestContent.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart != std::string::npos && manifestContent.substr(valueStart, 4) == "true")
        {
            return true;
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace updater

namespace
{
updater::UpdaterService* g_updaterService = nullptr;
}

updater::UpdaterService* UpdaterService_Get() { return g_updaterService; }

void UpdaterService_Set(updater::UpdaterService* service) { g_updaterService = service; }
