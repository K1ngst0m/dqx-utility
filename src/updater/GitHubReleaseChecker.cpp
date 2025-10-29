#include "GitHubReleaseChecker.hpp"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <plog/Log.h>

#include <atomic>
#include <thread>

using json = nlohmann::json;

namespace updater
{

struct GitHubReleaseChecker::Impl
{
    std::string owner;
    std::string repo;
    std::atomic<bool> cancelled{ false };
    std::thread checkThread;

    Impl(const std::string& o, const std::string& r)
        : owner(o)
        , repo(r)
    {
    }

    ~Impl()
    {
        cancelled = true;
        if (checkThread.joinable())
        {
            checkThread.join();
        }
    }

    std::string getApiUrl() const
    {
        // Use jsDelivr CDN to fetch version.json from main branch (no rate limits)
        // Note: @latest only works for npm packages, must use branch name for git repos
        return "https://cdn.jsdelivr.net/gh/" + owner + "/" + repo + "@main/version.json";
    }

    bool parseReleaseJson(const json& releaseJson, const Version& currentVersion, UpdateInfo& outInfo,
                          std::string& outError)
    {
        try
        {
            // Extract version from "version" field (simplified format)
            if (!releaseJson.contains("version"))
            {
                outError = "version.json missing 'version' field";
                return false;
            }

            std::string versionStr = releaseJson["version"];
            Version releaseVersion(versionStr);

            // Check if this is a newer version
            if (releaseVersion <= currentVersion)
            {
                PLOG_INFO << "Current version " << currentVersion.toString()
                          << " is up to date (latest: " << releaseVersion.toString() << ")";
                return false; // Not an error, just no update available
            }

            // Fill UpdateInfo
            outInfo.version = releaseVersion.toString();
            outInfo.changelogUrl = releaseJson.value("changelog_url", "");
            outInfo.releasedDate = releaseJson.value("published_at", "");

            // Get download URL
            if (!releaseJson.contains("download_url"))
            {
                outError = "version.json missing 'download_url' field";
                return false;
            }

            outInfo.downloadUrl = releaseJson["download_url"].get<std::string>();

            if (outInfo.downloadUrl.empty())
            {
                outError = "download_url is empty in version.json";
                return false;
            }

            // Package size not provided in version.json, will be determined during download
            outInfo.packageSize = 0;

            PLOG_INFO << "New version available: " << outInfo.version << " (current: " << currentVersion.toString()
                      << ")";
            PLOG_INFO << "Download URL: " << outInfo.downloadUrl;
            return true;
        }
        catch (const json::exception& e)
        {
            outError = std::string("JSON parse error: ") + e.what();
            PLOG_ERROR << outError;
            return false;
        }
    }
};

GitHubReleaseChecker::GitHubReleaseChecker(const std::string& owner, const std::string& repo)
    : impl_(std::make_unique<Impl>(owner, repo))
{
}

GitHubReleaseChecker::~GitHubReleaseChecker() = default;

void GitHubReleaseChecker::checkLatestReleaseAsync(const Version& currentVersion, ReleaseCheckCallback callback)
{
    if (!callback)
    {
        PLOG_ERROR << "GitHubReleaseChecker: callback is null";
        return;
    }

    impl_->cancelled = false;

    // Launch background thread
    impl_->checkThread = std::thread(
        [this, currentVersion, callback]()
        {
            UpdateInfo info;
            std::string error;
            bool success = checkLatestRelease(currentVersion, info, error);

            if (!impl_->cancelled)
            {
                callback(success, info, error);
            }
        });

    impl_->checkThread.detach();
}

bool GitHubReleaseChecker::checkLatestRelease(const Version& currentVersion, UpdateInfo& outInfo, std::string& outError)
{
    PLOG_INFO << "Checking GitHub for updates: " << impl_->owner << "/" << impl_->repo;

    try
    {
        // Fetch version.json from jsDelivr CDN
        auto response = cpr::Get(cpr::Url{impl_->getApiUrl()},
                                 cpr::Header{{"User-Agent", "DQX-Utility-Updater"}},
                                 cpr::Timeout{5000} // 5 second timeout
        );

        if (impl_->cancelled)
        {
            outError = "Check cancelled";
            return false;
        }

        // Check HTTP status
        if (response.status_code != 200)
        {
            outError = "Update check returned status " + std::to_string(response.status_code);
            if (response.status_code == 404)
            {
                outError += " (version.json not found)";
            }
            PLOG_ERROR << outError;
            return false;
        }

        // Parse JSON response
        json releaseJson = json::parse(response.text);
        return impl_->parseReleaseJson(releaseJson, currentVersion, outInfo, outError);
    }
    catch (const json::exception& e)
    {
        outError = std::string("JSON parse error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Network error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

void GitHubReleaseChecker::cancel() { impl_->cancelled = true; }

} // namespace updater
