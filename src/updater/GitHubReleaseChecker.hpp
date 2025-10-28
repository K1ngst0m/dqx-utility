#pragma once

#include "UpdateTypes.hpp"
#include "Version.hpp"

#include <functional>
#include <memory>
#include <string>

namespace updater
{

// Callback for release check results
using ReleaseCheckCallback = std::function<void(bool success, const UpdateInfo& info, const std::string& error)>;

// GitHub Releases API client
class GitHubReleaseChecker
{
public:
    GitHubReleaseChecker(const std::string& owner, const std::string& repo);
    ~GitHubReleaseChecker();

    // Check for latest release (async, non-blocking)
    // Calls callback with success status, update info, and error message
    void checkLatestReleaseAsync(const Version& currentVersion, ReleaseCheckCallback callback);

    // Check for latest release (blocking, for testing)
    bool checkLatestRelease(const Version& currentVersion, UpdateInfo& outInfo, std::string& outError);

    // Cancel ongoing check
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace updater
