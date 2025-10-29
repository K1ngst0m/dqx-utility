#include "ConfigMerger.hpp"

#include <plog/Log.h>
#include <toml++/toml.h>

#include <fstream>
#include <sstream>

namespace updater
{

static void deepMerge(toml::table& result, const toml::table& template_table)
{
    for (auto&& [key, value] : template_table)
    {
        if (value.is_table())
        {
            if (result.contains(key) && result[key].is_table())
            {
                deepMerge(*result[key].as_table(), *value.as_table());
            }
            else
            {
                result.insert_or_assign(key, value);
            }
        }
        else
        {
            if (!result.contains(key))
            {
                result.insert_or_assign(key, value);
            }
        }
    }
}

bool ConfigMerger::mergeConfigs(const std::string& existingConfigPath, const std::string& templateConfigPath,
                                const std::string& outputConfigPath, std::string& outError)
{
    try
    {
        toml::table existingConfig = toml::parse_file(existingConfigPath);
        toml::table templateConfig = toml::parse_file(templateConfigPath);

        deepMerge(existingConfig, templateConfig);

        std::ofstream outFile(outputConfigPath);
        if (!outFile.is_open())
        {
            outError = "Failed to open output file: " + outputConfigPath;
            return false;
        }

        outFile << existingConfig;
        outFile.close();

        PLOG_INFO << "Config merged successfully: " << outputConfigPath;
        return true;
    }
    catch (const toml::parse_error& e)
    {
        outError = std::string("TOML parse error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Config merge error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

bool ConfigMerger::mergeConfigStrings(const std::string& existingConfig, const std::string& templateConfig,
                                      std::string& outMergedConfig, std::string& outError)
{
    try
    {
        toml::table existing = toml::parse(existingConfig);
        toml::table template_table = toml::parse(templateConfig);

        deepMerge(existing, template_table);

        std::ostringstream oss;
        oss << existing;
        outMergedConfig = oss.str();

        return true;
    }
    catch (const toml::parse_error& e)
    {
        outError = std::string("TOML parse error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
    catch (const std::exception& e)
    {
        outError = std::string("Config merge error: ") + e.what();
        PLOG_ERROR << outError;
        return false;
    }
}

} // namespace updater
