#pragma once

#include "../updater/UpdateTypes.hpp"

#include <string>

namespace utils
{

// Cross-platform ZIP extraction using miniz library
class ZipExtractor
{
public:
    // Extract ZIP archive to target directory
    // Respects manifest.files[].preserve flag to skip preserved files (e.g., config.toml)
    // Returns true on success, false on failure with error message in outError
    static bool ExtractZip(const std::string& zipPath, const std::string& targetDir,
                           const updater::UpdateManifest& manifest, std::string& outError);
};

} // namespace utils
