#pragma once

#include <string>
#include <unordered_set>

class UnknownLabelRepository {
public:
    explicit UnknownLabelRepository(const std::string& path = "unknown_labels.txt");
    bool load(std::unordered_set<std::string>& out_labels) const;
    bool save(const std::unordered_set<std::string>& labels) const;

private:
    std::string path_;
};
