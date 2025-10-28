#include "ManifestParser.hpp"

#include <nlohmann/json.hpp>
#include <plog/Log.h>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace updater
{

bool ManifestParser::parse(const std::string& jsonContent, UpdateManifest& outManifest, std::string& outError)
{
    try
    {
        json manifestJson = json::parse(jsonContent);

        // Extract required fields
        if (!manifestJson.contains("version"))
        {
            outError = "Manifest missing 'version' field";
            return false;
        }

        outManifest.version = manifestJson["version"];
        outManifest.packageSha256 = manifestJson.value("package_sha256", "");
        outManifest.buildDate = manifestJson.value("build_date", "");

        // Parse files array
        if (manifestJson.contains("files") && manifestJson["files"].is_array())
        {
            for (const auto& fileJson : manifestJson["files"])
            {
                ManifestFile file;
                file.path = fileJson.value("path", "");

                // Check if this is a preserve-only file (like config.toml)
                if (fileJson.contains("action") && fileJson["action"] == "preserve")
                {
                    file.preserve = true;
                    file.sha256 = "";
                    file.size = 0;
                }
                else
                {
                    file.sha256 = fileJson.value("sha256", "");
                    file.size = fileJson.value("size", 0);
                    file.preserve = false;
                }

                if (file.path.empty())
                {
                    PLOG_WARNING << "Skipping manifest file entry with empty path";
                    continue;
                }

                outManifest.files.push_back(file);
            }
        }

        // Validate parsed manifest
        if (!validate(outManifest, outError))
        {
            return false;
        }

        PLOG_INFO << "Manifest parsed successfully: version " << outManifest.version << " with "
                  << outManifest.files.size() << " files";
        return true;
    }
    catch (const json::exception& e)
    {
        outError = std::string("JSON parse error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool ManifestParser::parseFile(const std::string& filePath, UpdateManifest& outManifest, std::string& outError)
{
    try
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            outError = "Failed to open manifest file: " + filePath;
            PLOG_ERROR << outError;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return parse(buffer.str(), outManifest, outError);
    }
    catch (const std::exception& e)
    {
        outError = std::string("File read error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool ManifestParser::validate(const UpdateManifest& manifest, std::string& outError)
{
    // Check required fields
    if (manifest.version.empty())
    {
        outError = "Manifest version is empty";
        return false;
    }

    if (manifest.files.empty())
    {
        outError = "Manifest has no files";
        return false;
    }

    // Validate each file entry
    for (const auto& file : manifest.files)
    {
        if (file.path.empty())
        {
            outError = "Manifest contains file with empty path";
            return false;
        }

        // If not preserve-only, check for checksum
        if (!file.preserve && file.sha256.empty())
        {
            outError = "File '" + file.path + "' missing SHA-256 checksum";
            return false;
        }

        // Validate SHA-256 format (should be 64 hex characters)
        if (!file.sha256.empty() && file.sha256.length() != 64)
        {
            outError = "File '" + file.path + "' has invalid SHA-256 checksum length";
            return false;
        }
    }

    return true;
}

} // namespace updater
