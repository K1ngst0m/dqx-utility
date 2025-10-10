#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include "UnknownLabelRepository.hpp"

class LabelProcessor
{
public:
    explicit LabelProcessor(UnknownLabelRepository* repo = nullptr);
    ~LabelProcessor();

    std::string processText(const std::string& input);
    
    // Persistence is delegated to UnknownLabelRepository now.
    const std::unordered_set<std::string>& getUnknownLabels() const { return unknown_labels_; }

private:
    std::string processKnownLabels(const std::string& input);
    std::string processIgnoredLabels(const std::string& input);
    std::string trackUnknownLabels(const std::string& input);
    
    std::vector<std::string> extractLabels(const std::string& input);
    bool isKnownLabel(const std::string& label);
    bool isIgnoredLabel(const std::string& label);
    
    std::string processSelectSection(const std::string& content);
    
    std::unordered_set<std::string> unknown_labels_;
    UnknownLabelRepository* repository_ = nullptr;
};
