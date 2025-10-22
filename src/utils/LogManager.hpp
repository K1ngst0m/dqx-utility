#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <plog/Severity.h>

namespace plog
{
class IAppender;
}

namespace utils
{

class LogManager
{
public:
    struct LoggerConfig
    {
        std::string name;
        std::string filepath;
        std::optional<bool> append_override;
        std::optional<plog::Severity> level_override;
        size_t max_file_size = 10 * 1024 * 1024;
        size_t backup_count = 3;
        bool add_console_appender = false;
    };

    static bool Initialize();

    template<int InstanceId = 0>
    static bool RegisterLogger(const LoggerConfig& config);

    static void Shutdown();

    static bool IsAppendMode();
    static plog::Severity GetDefaultLogLevel();
    static void PrepareLogDirectory();

private:
    LogManager() = default;

    static bool ReadConfig();

    static bool s_initialized;
    static bool s_append_logs;
    static plog::Severity s_default_level;
    static std::vector<std::unique_ptr<plog::IAppender>> s_appenders;
};

} // namespace utils
