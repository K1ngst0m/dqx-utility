#pragma once

#include <string>
#include <unordered_set>
#include <vector>

class LabelProcessor
{
public:
    LabelProcessor();
    ~LabelProcessor();

    std::string processText(const std::string& input);
    
    void loadUnknownLabels();
    void saveUnknownLabels();
    
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
    static constexpr const char* kUnknownLabelsFile = "unknown_labels.txt";
};