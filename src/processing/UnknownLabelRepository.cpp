#include "UnknownLabelRepository.hpp"

#include "../utils/ErrorReporter.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>

namespace
{
    std::mutex g_repo_mutex;
    std::unordered_set<std::string> g_load_warning_paths;
    std::unordered_set<std::string> g_save_error_paths;
}

UnknownLabelRepository::UnknownLabelRepository(const std::string& path)
    : path_(path)
{
}

bool UnknownLabelRepository::load(std::unordered_set<std::string>& out_labels) const
{
    std::ifstream file(path_);
    if (!file.is_open())
    {
        std::error_code ec;
        if (std::filesystem::exists(path_, ec))
        {
            std::lock_guard<std::mutex> lock(g_repo_mutex);
            if (g_load_warning_paths.insert(path_).second)
            {
                utils::ErrorReporter::ReportWarning(utils::ErrorCategory::Configuration,
                    "Failed to read unknown label cache",
                    path_);
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
    // If no labels, still write an empty file (keeping behavior simple).
    std::ofstream file(path_, std::ios::trunc);
    if (!file.is_open())
    {
        std::lock_guard<std::mutex> lock(g_repo_mutex);
        if (g_save_error_paths.insert(path_).second)
        {
            utils::ErrorReporter::ReportError(utils::ErrorCategory::Configuration,
                "Failed to write unknown label cache",
                path_);
        }
        return false;
    }

    for (const auto& label : labels)
    {
        file << label << "\n";
    }

    return true;
}
