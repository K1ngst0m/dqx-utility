#pragma once

#include <string>

namespace updater
{

class ConfigMerger
{
public:
    ConfigMerger() = default;
    ~ConfigMerger() = default;

    bool mergeConfigs(const std::string& existingConfigPath, const std::string& templateConfigPath,
                      const std::string& outputConfigPath, std::string& outError);

    bool mergeConfigStrings(const std::string& existingConfig, const std::string& templateConfig,
                            std::string& outMergedConfig, std::string& outError);
};

} // namespace updater
