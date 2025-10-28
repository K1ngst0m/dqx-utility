#include "Version.hpp"

#include <sstream>
#include <regex>

namespace updater
{

Version::Version() : major_(0), minor_(0), patch_(0)
{
}

Version::Version(int major, int minor, int patch) : major_(major), minor_(minor), patch_(patch)
{
}

Version::Version(const std::string& versionString) : major_(0), minor_(0), patch_(0)
{
    if (!parseString(versionString))
    {
        // Keep default 0.0.0 if parsing fails
    }
}

std::string Version::toString() const
{
    std::ostringstream oss;
    oss << major_ << "." << minor_ << "." << patch_;
    return oss.str();
}

bool Version::tryParse(const std::string& versionString, Version& outVersion)
{
    return outVersion.parseString(versionString);
}

bool Version::parseString(const std::string& versionString)
{
    // Support formats:
    // - "1.2.3"
    // - "v1.2.3"
    // - "1.2" (patch defaults to 0)
    // - "1" (minor and patch default to 0)

    // Remove leading 'v' or 'V'
    std::string cleaned = versionString;
    if (!cleaned.empty() && (cleaned[0] == 'v' || cleaned[0] == 'V'))
    {
        cleaned = cleaned.substr(1);
    }

    // Use regex to parse version components
    std::regex versionRegex(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?$)");
    std::smatch match;

    if (std::regex_match(cleaned, match, versionRegex))
    {
        try
        {
            // match[0] is full match, match[1] is major, match[2] is minor, match[3] is patch
            major_ = std::stoi(match[1].str());
            minor_ = match[2].matched ? std::stoi(match[2].str()) : 0;
            patch_ = match[3].matched ? std::stoi(match[3].str()) : 0;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    return false;
}

} // namespace updater
