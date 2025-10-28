#pragma once

#include "UpdateTypes.hpp"

#include <string>

namespace updater
{

// Parser for update package manifest.json
class ManifestParser
{
public:
    ManifestParser() = default;
    ~ManifestParser() = default;

    // Parse manifest from JSON string
    bool parse(const std::string& jsonContent, UpdateManifest& outManifest, std::string& outError);

    // Parse manifest from file
    bool parseFile(const std::string& filePath, UpdateManifest& outManifest, std::string& outError);

    // Verify manifest structure and required fields
    static bool validate(const UpdateManifest& manifest, std::string& outError);
};

} // namespace updater
