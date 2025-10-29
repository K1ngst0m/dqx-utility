#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace updater
{

// Update state machine
enum class UpdateState
{
    Idle, // No update activity
    Checking, // Checking GitHub for updates
    Available, // Update available, not downloaded
    Downloading, // Download in progress
    Downloaded, // Package ready to apply
    Applying, // Update being applied
    Failed, // Update failed
    Completed // Update completed successfully
};

// Information about an available update
struct UpdateInfo
{
    std::string version; // e.g., "0.2.0"
    std::string downloadUrl; // GitHub release asset URL
    std::string changelogUrl; // GitHub release page URL
    size_t packageSize; // Size in bytes
    std::string releasedDate; // ISO 8601 format

    UpdateInfo()
        : packageSize(0)
    {
    }
};

// Download progress information
struct DownloadProgress
{
    size_t bytesDownloaded; // Bytes downloaded so far
    size_t totalBytes; // Total package size
    float percentage; // Download percentage (0-100)
    std::string speed; // Human-readable speed (e.g., "2.5 MB/s")

    DownloadProgress()
        : bytesDownloaded(0)
        , totalBytes(0)
        , percentage(0.0f)
    {
    }
};

// Error information for failed updates
struct UpdateError
{
    std::string message; // Human-readable error message
    std::string technicalInfo; // Technical details for logging
    int errorCode; // Platform-specific error code

    UpdateError()
        : errorCode(0)
    {
    }

    UpdateError(const std::string& msg)
        : message(msg)
        , errorCode(0)
    {
    }

    UpdateError(const std::string& msg, const std::string& tech, int code)
        : message(msg)
        , technicalInfo(tech)
        , errorCode(code)
    {
    }
};

// File entry in update manifest
struct ManifestFile
{
    std::string path; // Relative path in package
    std::string sha256; // SHA-256 checksum (hex string)
    size_t size; // File size in bytes
    bool preserve; // True if file should be preserved (e.g., config.toml)

    ManifestFile()
        : size(0)
        , preserve(false)
    {
    }
};

// Update package manifest
struct UpdateManifest
{
    std::string version; // Package version
    std::string packageSha256; // SHA-256 of entire ZIP package
    std::string buildDate; // ISO 8601 format
    std::vector<ManifestFile> files; // List of files in package

    UpdateManifest() = default;
};

} // namespace updater
