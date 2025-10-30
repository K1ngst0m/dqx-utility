#include "ZipExtractor.hpp"

#include <plog/Log.h>

#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

namespace utils
{

bool ZipExtractor::ExtractZip(const std::string& zipPath, const std::string& targetDir,
                               const updater::UpdateManifest& manifest, std::string& outError)
{
    if (!fs::exists(zipPath))
    {
        outError = "ZIP file does not exist: " + zipPath;
        PLOG_ERROR << outError;
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
    {
        outError = "Failed to open ZIP archive: " + zipPath;
        PLOG_ERROR << outError;
        return false;
    }

    std::set<std::string> preserveFiles;
    for (const auto& file : manifest.files)
    {
        if (file.preserve)
        {
            preserveFiles.insert(file.path);
            PLOG_DEBUG << "Skipping preserved file: " << file.path;
        }
    }

    int fileCount = mz_zip_reader_get_num_files(&zip);
    PLOG_INFO << "Extracting " << fileCount << " files from ZIP archive";

    std::string zipRootDir;
    for (int i = 0; i < fileCount; ++i)
    {
        mz_zip_archive_file_stat fileStat{};
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat))
            continue;

        std::string filename = fileStat.m_filename;

        size_t pos = filename.rfind("manifest.json");
        if (pos != std::string::npos && pos + 13 == filename.length())
        {
            if (pos > 0)
            {
                zipRootDir = filename.substr(0, pos);
                PLOG_INFO << "Found manifest at: " << filename;
                PLOG_INFO << "ZIP root directory: '" << zipRootDir << "'";
            }
            else
            {
                PLOG_INFO << "Manifest at root level";
            }
            break;
        }
    }

    for (int i = 0; i < fileCount; ++i)
    {
        mz_zip_archive_file_stat fileStat{};
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat))
        {
            outError = "Failed to read file stat from ZIP";
            PLOG_ERROR << outError;
            mz_zip_reader_end(&zip);
            return false;
        }

        std::string filename = fileStat.m_filename;

        if (fileStat.m_is_directory)
        {
            continue;
        }

        std::string relativePath = filename;
        if (!zipRootDir.empty())
        {
            if (filename.compare(0, zipRootDir.length(), zipRootDir) == 0)
            {
                relativePath = filename.substr(zipRootDir.length());
            }
        }

        std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

        if (preserveFiles.count(relativePath) > 0)
        {
            PLOG_INFO << "Skipping preserved: " << relativePath;
            continue;
        }

        fs::path destPath = fs::path(targetDir) / relativePath;

        PLOG_INFO << "Extracting: '" << filename << "' -> '" << destPath.string() << "'";

        fs::create_directories(destPath.parent_path());

        void* fileData = mz_zip_reader_extract_to_heap(&zip, i, nullptr, 0);
        if (!fileData)
        {
            outError = "Failed to extract file: " + filename;
            PLOG_ERROR << outError;
            mz_zip_reader_end(&zip);
            return false;
        }

        std::ofstream outFile(destPath, std::ios::binary);
        if (!outFile)
        {
            outError = "Failed to create file: " + destPath.string();
            PLOG_ERROR << outError;
            PLOG_ERROR << "Filename in ZIP: '" << filename << "'";
            PLOG_ERROR << "Relative path: '" << relativePath << "'";
            PLOG_ERROR << "ZIP root dir: '" << zipRootDir << "'";
            PLOG_ERROR << "Target dir: '" << targetDir << "'";
            mz_free(fileData);
            mz_zip_reader_end(&zip);
            return false;
        }

        outFile.write(static_cast<const char*>(fileData), fileStat.m_uncomp_size);
        outFile.close();

        mz_free(fileData);
    }

    mz_zip_reader_end(&zip);

    PLOG_INFO << "ZIP extraction completed successfully";
    return true;
}

} // namespace utils
