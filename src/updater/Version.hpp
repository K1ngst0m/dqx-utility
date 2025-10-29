#pragma once

#include <compare>
#include <string>

namespace updater
{

// Semantic version (major.minor.patch)
class Version
{
public:
    // Construct from version string (e.g., "0.1.0", "v1.2.3")
    explicit Version(const std::string& versionString);

    // Construct from components
    Version(int major, int minor, int patch);

    // Default constructor (0.0.0)
    Version();

    // Getters
    int major() const { return major_; }

    int minor() const { return minor_; }

    int patch() const { return patch_; }

    // Convert to string (e.g., "0.1.0")
    std::string toString() const;

    // Comparison operators
    auto operator<=>(const Version& other) const = default;

    // Parse version string (returns true if valid)
    static bool tryParse(const std::string& versionString, Version& outVersion);

private:
    int major_;
    int minor_;
    int patch_;

    // Parse version string helper
    bool parseString(const std::string& versionString);
};

} // namespace updater
