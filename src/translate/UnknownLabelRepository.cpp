#include "UnknownLabelRepository.hpp"

#include <fstream>
#include <string>

UnknownLabelRepository::UnknownLabelRepository(const std::string& path)
    : path_(path)
{
}

bool UnknownLabelRepository::load(std::unordered_set<std::string>& out_labels) const
{
    std::ifstream file(path_);
    if (!file.is_open())
        return false;

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
        return false;

    for (const auto& label : labels)
    {
        file << label << "\n";
    }

    return true;
}
