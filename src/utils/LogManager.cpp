#include "LogManager.hpp"
#include "ErrorReporter.hpp"
#include "../processing/Diagnostics.hpp"
#include "Profile.hpp"

#include <filesystem>
#include <fstream>

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <toml++/toml.h>

namespace utils
{

bool LogManager::s_initialized = false;
bool LogManager::s_append_logs = true;
plog::Severity LogManager::s_default_level = plog::info;
std::vector<std::unique_ptr<plog::IAppender>> LogManager::s_appenders;

bool LogManager::Initialize()
{
    if (s_initialized)
        return true;

    if (!ReadConfig())
        return false;

    PrepareLogDirectory();

    if (!s_append_logs)
    {
        std::ofstream marker(".dqx_append_logs");
        marker << "false";
        marker.close();
    }

    s_initialized = true;
    return true;
}

template <int InstanceId>
bool LogManager::RegisterLogger(const LoggerConfig& config)
{
    if (!s_initialized)
    {
        ErrorReporter::ReportError(ErrorCategory::Initialization,
                                   "LogManager not initialized before registering logger", config.name);
        return false;
    }

    try
    {
        bool append = config.append_override.value_or(s_append_logs);
        if (!append)
        {
            std::ofstream(config.filepath, std::ios::trunc).close();
        }

        auto file_appender = std::make_unique<plog::RollingFileAppender<plog::TxtFormatter>>(
            config.filepath.c_str(), config.max_file_size, config.backup_count);

        plog::Severity level = config.level_override.value_or(s_default_level);

        plog::init<InstanceId>(level, file_appender.get());

        if (config.add_console_appender)
        {
            auto console_appender = std::make_unique<plog::ConsoleAppender<plog::TxtFormatter>>();
            if (auto logger = plog::get<InstanceId>())
            {
                logger->addAppender(console_appender.get());
                s_appenders.push_back(std::move(console_appender));
            }
        }

        s_appenders.push_back(std::move(file_appender));
        return true;
    }
    catch (const std::exception& ex)
    {
        ErrorReporter::ReportError(ErrorCategory::Initialization, "Failed to register logger: " + config.name,
                                   ex.what());
        return false;
    }
}

template bool LogManager::RegisterLogger<0>(const LoggerConfig&);
template bool LogManager::RegisterLogger<processing::Diagnostics::kLogInstance>(const LoggerConfig&);

#if DQX_PROFILING_LEVEL >= 1
template bool LogManager::RegisterLogger<profiling::kProfilingLogInstance>(const LoggerConfig&);
#endif

void LogManager::Shutdown()
{
    s_appenders.clear();
    s_initialized = false;
}

bool LogManager::IsAppendMode() { return s_append_logs; }

plog::Severity LogManager::GetDefaultLogLevel() { return s_default_level; }

void LogManager::PrepareLogDirectory()
{
    std::error_code ec;
    std::filesystem::create_directories("logs", ec);
    if (ec)
    {
        ErrorReporter::ReportWarning(ErrorCategory::Initialization, "Unable to prepare log directory", ec.message());
    }
}

bool LogManager::ReadConfig()
{
    try
    {
        auto cfg = toml::parse_file("config.toml");
        if (auto global = cfg["global"].as_table())
        {
            if (auto append = (*global)["append_logs"].value<bool>())
            {
                s_append_logs = *append;
            }
        }

        if (auto debug = cfg["app"]["debug"].as_table())
        {
            if (auto level = (*debug)["logging_level"].value<int64_t>())
            {
                int level_int = static_cast<int>(*level);
                if (level_int >= 0 && level_int <= 6)
                {
                    s_default_level = static_cast<plog::Severity>(level_int);
                }
            }
        }

        return true;
    }
    catch (...)
    {
        return true;
    }
}

} // namespace utils
