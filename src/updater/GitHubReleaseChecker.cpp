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
    std::atomic<bool> cancelled{false};
    std::thread checkThread;

    Impl(const std::string& o, const std::string& r) : owner(o), repo(r) {}

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
        return "https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest";
    }

    bool parseReleaseJson(const json& releaseJson, const Version& currentVersion, UpdateInfo& outInfo,
                          std::string& outError)
    {
        try
        {
            // Extract version from tag_name
            if (!releaseJson.contains("tag_name"))
            {
                outError = "Release JSON missing tag_name";
                return false;
            }

            std::string tagName = releaseJson["tag_name"];
            Version releaseVersion(tagName);

            // Check if this is a newer version
            if (releaseVersion <= currentVersion)
            {
                PLOG_INFO << "Current version " << currentVersion.toString() << " is up to date (latest: "
                          << releaseVersion.toString() << ")";
                return false; // Not an error, just no update available
            }

            // Fill UpdateInfo
            outInfo.version = releaseVersion.toString();
            outInfo.changelogUrl = releaseJson.value("html_url", "");
            outInfo.releasedDate = releaseJson.value("published_at", "");

            // Find the package asset (look for .zip file matching pattern)
            if (releaseJson.contains("assets") && releaseJson["assets"].is_array())
            {
                for (const auto& asset : releaseJson["assets"])
                {
                    std::string assetName = asset.value("name", "");

                    // Look for: dqx-utility-zh-CN-*.zip or similar
                    if (assetName.find(".zip") != std::string::npos &&
                        assetName.find("dqx-utility") != std::string::npos)
                    {
                        outInfo.downloadUrl = asset.value("browser_download_url", "");
                        outInfo.packageSize = asset.value("size", 0);
                        break;
                    }
                }
            }

            if (outInfo.downloadUrl.empty())
            {
                outError = "No suitable package found in release assets";
                return false;
            }

            PLOG_INFO << "New version available: " << outInfo.version << " (current: " << currentVersion.toString()
                      << ")";
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
    impl_->checkThread = std::thread([this, currentVersion, callback]() {
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

bool GitHubReleaseChecker::checkLatestRelease(const Version& currentVersion, UpdateInfo& outInfo,
                                               std::string& outError)
{
    PLOG_INFO << "Checking GitHub for updates: " << impl_->owner << "/" << impl_->repo;

    try
    {
        // Make API request
        auto response = cpr::Get(cpr::Url{impl_->getApiUrl()},
                                 cpr::Header{{"Accept", "application/vnd.github+json"},
                                             {"User-Agent", "DQX-Utility-Updater"}},
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
            outError = "GitHub API returned status " + std::to_string(response.status_code);
            if (response.status_code == 404)
            {
                outError += " (repository or release not found)";
            }
            else if (response.status_code == 403)
            {
                outError += " (rate limit exceeded)";
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

void GitHubReleaseChecker::cancel()
{
    impl_->cancelled = true;
}

} // namespace updater
