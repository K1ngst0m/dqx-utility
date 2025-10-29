#include "PackageDownloader.hpp"

#include <cpr/cpr.h>
#include <plog/Log.h>

#include <picosha2.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

namespace updater
{

struct PackageDownloader::Impl
{
    std::atomic<bool> downloading{ false };
    std::atomic<bool> cancelled{ false };
    std::thread downloadThread;

    ~Impl()
    {
        cancelled = true;
        if (downloadThread.joinable())
        {
            downloadThread.join();
        }
    }

    std::string formatSpeed(double bytesPerSecond)
    {
        if (bytesPerSecond < 1024)
            return std::to_string(static_cast<int>(bytesPerSecond)) + " B/s";
        if (bytesPerSecond < 1024 * 1024)
            return std::to_string(static_cast<int>(bytesPerSecond / 1024)) + " KB/s";
        return std::to_string(static_cast<int>(bytesPerSecond / (1024 * 1024))) + " MB/s";
    }
};

PackageDownloader::PackageDownloader()
    : impl_(std::make_unique<Impl>())
{
}

PackageDownloader::~PackageDownloader() = default;

void PackageDownloader::downloadAsync(const std::string& url, const std::string& destPath,
                                      PackageProgressCallback progressCallback,
                                      PackageDownloadCallback completeCallback)
{
    if (impl_->downloading)
    {
        PLOG_WARNING << "Download already in progress";
        if (completeCallback)
        {
            completeCallback(false, "", "Download already in progress");
        }
        return;
    }

    impl_->downloading = true;
    impl_->cancelled = false;

    impl_->downloadThread = std::thread(
        [this, url, destPath, progressCallback, completeCallback]()
        {
            PLOG_INFO << "Starting download: " << url;

            std::ofstream outputFile(destPath, std::ios::binary);
            if (!outputFile.is_open())
            {
                PLOG_ERROR << "Failed to create output file: " << destPath;
                impl_->downloading = false;
                if (completeCallback)
                {
                    completeCallback(false, "", "Failed to create output file");
                }
                return;
            }

            auto startTime = std::chrono::steady_clock::now();
            size_t lastBytes = 0;
            auto lastProgressTime = startTime;

            cpr::Response response = cpr::Download(
                outputFile, cpr::Url{ url },
                cpr::ProgressCallback{
                    [&](cpr::cpr_off_t downloadTotal, cpr::cpr_off_t downloadNow, cpr::cpr_off_t, cpr::cpr_off_t,
                        intptr_t) -> bool
                    {
                        if (impl_->cancelled)
                        {
                            return false;
                        }

                        if (downloadTotal > 0 && progressCallback)
                        {
                            auto now = std::chrono::steady_clock::now();
                            auto elapsed =
                                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count();

                            if (elapsed >= 100)
                            {
                                double bytesPerSecond = 0.0;
                                if (elapsed > 0)
                                {
                                    bytesPerSecond = (downloadNow - lastBytes) * 1000.0 / elapsed;
                                }

                                DownloadProgress progress;
                                progress.bytesDownloaded = static_cast<size_t>(downloadNow);
                                progress.totalBytes = static_cast<size_t>(downloadTotal);
                                progress.percentage = (static_cast<float>(downloadNow) / downloadTotal) * 100.0f;
                                progress.speed = impl_->formatSpeed(bytesPerSecond);

                                progressCallback(progress);

                                lastBytes = downloadNow;
                                lastProgressTime = now;
                            }
                        }

                        return true;
                    } },
                cpr::Timeout{ 60000 });

            outputFile.close();

            impl_->downloading = false;

            if (impl_->cancelled)
            {
                PLOG_INFO << "Download cancelled";
                std::remove(destPath.c_str());
                if (completeCallback)
                {
                    completeCallback(false, "", "Download cancelled");
                }
                return;
            }

            if (response.status_code != 200)
            {
                PLOG_ERROR << "Download failed with status: " << response.status_code;
                std::remove(destPath.c_str());
                if (completeCallback)
                {
                    completeCallback(false, "", "HTTP error " + std::to_string(response.status_code));
                }
                return;
            }

            PLOG_INFO << "Download completed: " << destPath;
            if (completeCallback)
            {
                completeCallback(true, destPath, "");
            }
        });

    impl_->downloadThread.detach();
}

void PackageDownloader::cancel() { impl_->cancelled = true; }

bool PackageDownloader::isDownloading() const { return impl_->downloading.load(); }

bool PackageDownloader::verifyChecksum(const std::string& filePath, const std::string& expectedSha256,
                                       std::string& outError)
{
    try
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            outError = "Failed to open file for checksum verification";
            return false;
        }

        std::vector<unsigned char> hash(picosha2::k_digest_size);
        picosha2::hash256(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), hash.begin(),
                          hash.end());

        std::string actualSha256 = picosha2::bytes_to_hex_string(hash.begin(), hash.end());

        if (actualSha256 != expectedSha256)
        {
            outError = "Checksum mismatch: expected " + expectedSha256 + ", got " + actualSha256;
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Checksum verification error: ") + e.what();
        return false;
    }
}

} // namespace updater
