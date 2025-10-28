#pragma once

#include "UpdateTypes.hpp"

#include <functional>
#include <memory>
#include <string>

namespace updater
{

using PackageDownloadCallback = std::function<void(bool success, const std::string& filePath, const std::string& error)>;
using PackageProgressCallback = std::function<void(const DownloadProgress&)>;

class PackageDownloader
{
public:
    PackageDownloader();
    ~PackageDownloader();

    void downloadAsync(const std::string& url, const std::string& destPath,
                      PackageProgressCallback progressCallback,
                      PackageDownloadCallback completeCallback);

    void cancel();
    bool isDownloading() const;

    static bool verifyChecksum(const std::string& filePath, const std::string& expectedSha256, std::string& outError);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace updater
