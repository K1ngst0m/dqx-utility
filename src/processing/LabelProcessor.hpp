#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "UnknownLabelRepository.hpp"

// Forward declaration
namespace label_processing {
    class LabelRegistry;
}

class LabelProcessor
{
public:
    explicit LabelProcessor(UnknownLabelRepository* repo = nullptr);
    ~LabelProcessor();

    [[nodiscard]] std::string processText(const std::string& input);
    
    [[nodiscard]] const std::unordered_set<std::string>& getUnknownLabels() const noexcept { return unknown_labels_; }

private:
    std::string processKnownLabels(const std::string& input);
    std::string processPairedLabels(const std::string& input);
    std::string processStandaloneLabels(const std::string& input);
    std::string trackUnknownLabels(const std::string& input);
    
    std::vector<std::string> extractLabels(const std::string& input);
    bool isKnownLabel(const std::string& label);
    bool isIgnoredLabel(const std::string& label);
    
    std::string processSelectSection(const std::string& content);
    
    std::unordered_set<std::string> unknown_labels_;
    UnknownLabelRepository* repository_ = nullptr;
    std::unique_ptr<label_processing::LabelRegistry> registry_;
};
