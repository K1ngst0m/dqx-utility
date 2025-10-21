#include "UnknownLabelRepository.hpp"

#include "../utils/ErrorReporter.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>
#include <unordered_map>

namespace
{
std::mutex g_repo_mutex;
constexpr size_t MAX_WARNING_CACHE_SIZE = 100;
std::unordered_map<std::string, size_t> g_load_warnings;
std::unordered_map<std::string, size_t> g_save_errors;
} // namespace

UnknownLabelRepository::UnknownLabelRepository(const std::string& path)
    : path_(path)
{
}

bool UnknownLabelRepository::load(std::unordered_set<std::string>& out_labels) const
{
    std::error_code ec;
    bool file_exists = std::filesystem::exists(path_, ec);

    std::ifstream file(path_);
    if (!file.is_open())
    {
        if (file_exists && !ec)
        {
            bool should_warn = false;
            {
                std::lock_guard<std::mutex> lock(g_repo_mutex);
                auto& count = g_load_warnings[path_];
                should_warn = (count == 0);
                if (++count > MAX_WARNING_CACHE_SIZE)
                    g_load_warnings.erase(path_);
            }

            if (should_warn)
            {
                std::string detail = "Path: " + path_;
                if (ec)
                    detail += " | Error: " + ec.message();

                utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                                                    "Failed to read unknown label cache", detail);
            }
        }
        return false;
    }

    std::string label;
    while (std::getline(file, label))
    {
        if (!label.empty())
            out_labels.insert(label);
    }

    return true;
}

bool UnknownLabelRepository::save(const std::unordered_set<std::string>& labels) const
{
    std::error_code ec;
    auto parent_path = std::filesystem::path(path_).parent_path();
    if (!parent_path.empty())
    {
        std::filesystem::create_directories(parent_path, ec);
        if (ec)
        {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
                                              "Failed to create directory for unknown label cache",
                                              "Path: " + parent_path.string() + " | Error: " + ec.message());
            return false;
        }
    }

    std::ofstream file(path_, std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        bool should_report = false;
        {
            std::lock_guard<std::mutex> lock(g_repo_mutex);
            auto& count = g_save_errors[path_];
            should_report = (count < 3);
            if (++count > MAX_WARNING_CACHE_SIZE)
                g_save_errors.erase(path_);
        }

        if (should_report)
        {
            std::string detail = "Path: " + path_;
            if (ec)
                detail += " | Error: " + ec.message();

            utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
                                              "Failed to write unknown label cache", detail);
        }
        return false;
    }

    for (const auto& label : labels)
        file << label << "\n";

    if (!file.good())
    {
        utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration, "Error writing unknown label cache",
                                          "Path: " + path_);
        return false;
    }

    return true;
}
