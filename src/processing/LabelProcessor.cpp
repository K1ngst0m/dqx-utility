#include "LabelProcessor.hpp"
#include "LabelRegistry.hpp"
#include "StageRunner.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

LabelProcessor::LabelProcessor(UnknownLabelRepository* repo)
    : repository_(repo)
    , registry_(std::make_unique<label_processing::LabelRegistry>())
{
    if (repository_)
        repository_->load(unknown_labels_);
}

LabelProcessor::~LabelProcessor()
{
    if (repository_)
        repository_->save(unknown_labels_);
}

std::string LabelProcessor::processText(const std::string& input)
{
    // Stage 1: Process all known labels (transforms, removes, paired content)
    auto known_stage = processing::run_stage<std::string>("label_known", [&]() {
        return this->processKnownLabels(input);
    });
    if (!known_stage.succeeded) {
        return input; // fallback to original on failure
    }

    // Stage 2: Track and remove unknown labels
    auto unknown_stage = processing::run_stage<std::string>("label_unknowns", [&]() {
        return this->trackUnknownLabels(known_stage.result);
    });
    if (!unknown_stage.succeeded) {
        return known_stage.result;
    }

    return unknown_stage.result;
}

std::string LabelProcessor::processKnownLabels(const std::string& input)
{
    std::string result = input;
    
    // First pass: Handle paired labels (select blocks, attr blocks)
    result = processPairedLabels(result);
    
    // Second pass: Handle standalone labels (transforms and removes)
    result = processStandaloneLabels(result);
    
    return result;
}

std::string LabelProcessor::processPairedLabels(const std::string& input)
{
    std::string result;
    result.reserve(input.size());
    
    size_t pos = 0;
    while (pos < input.size()) {
        // Find next potential label
        size_t label_start = input.find('<', pos);
        if (label_start == std::string::npos) {
            // No more labels, append rest and break
            result.append(input, pos, std::string::npos);
            break;
        }
        
        // Append text before label
        result.append(input, pos, label_start - pos);
        
        // Find label end
        size_t label_end = input.find('>', label_start);
        if (label_end == std::string::npos) {
            // Malformed label, append and continue
            result.append(input, label_start, std::string::npos);
            break;
        }
        
        std::string label = input.substr(label_start, label_end - label_start + 1);
        const auto* def = registry_->findMatch(label);
        
        if (def && def->match_type == label_processing::LabelMatchType::Paired) {
            // This is a paired opening tag - find its closing tag (case-insensitive)
            std::string close_tag = def->pair_close;
            size_t content_start = label_end + 1;
            
            // Case-insensitive search for closing tag
            std::string lower_input_substr = input.substr(content_start);
            std::transform(lower_input_substr.begin(), lower_input_substr.end(),
                          lower_input_substr.begin(), ::tolower);
            std::string lower_close = close_tag;
            std::transform(lower_close.begin(), lower_close.end(),
                          lower_close.begin(), ::tolower);
            
            size_t close_offset = lower_input_substr.find(lower_close);
            
            if (close_offset != std::string::npos) {
                size_t close_pos = content_start + close_offset;
                
                // Extract content between tags
                std::string content = input.substr(content_start, close_pos - content_start);
                
                // Process according to action
                if (def->action == label_processing::LabelAction::ProcessPaired && def->processor) {
                    // Process and append the transformed content
                    result.append(def->processor(content));
                }
                // For Remove action, we just skip the entire block
                
                // Skip past closing tag
                pos = close_pos + close_tag.size();
                continue;
            } else {
                // Closing tag not found - treat as standalone
                result.append(label);
                pos = label_end + 1;
                continue;
            }
        }
        
        // Not a paired label or not matched - will be handled in next pass
        result.append(label);
        pos = label_end + 1;
    }
    
    return result;
}

std::string LabelProcessor::processStandaloneLabels(const std::string& input)
{
    std::string result;
    result.reserve(input.size());
    
    size_t pos = 0;
    while (pos < input.size()) {
        size_t label_start = input.find('<', pos);
        if (label_start == std::string::npos) {
            result.append(input, pos, std::string::npos);
            break;
        }
        
        result.append(input, pos, label_start - pos);
        
        size_t label_end = input.find('>', label_start);
        if (label_end == std::string::npos) {
            result.append(input, label_start, std::string::npos);
            break;
        }
        
        std::string label = input.substr(label_start, label_end - label_start + 1);
        const auto* def = registry_->findMatch(label);
        
        if (def && def->match_type != label_processing::LabelMatchType::Paired) {
            // Process standalone label
            std::string processed = registry_->processLabel(label, def);
            result.append(processed);
        } else {
            // Unknown or paired (already processed) - keep it for now
            result.append(label);
        }
        
        pos = label_end + 1;
    }
    
    return result;
}

std::string LabelProcessor::trackUnknownLabels(const std::string& input)
{
    std::vector<std::string> labels = extractLabels(input);
    std::unordered_set<std::string> unknown_to_remove;

    for (const auto& label : labels) {
        const auto* def = registry_->findMatch(label);
        
        if (!def) {
            // This is an unknown label
            unknown_labels_.insert(label);
            unknown_to_remove.insert(label);
        }
    }

    if (unknown_to_remove.empty())
        return input;

    // Remove unknown labels from the text
    std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ) {
        bool found_label = false;
        for (const auto& label : unknown_to_remove) {
            if (input.compare(i, label.size(), label) == 0) {
                i += label.size();
                found_label = true;
                break;
            }
        }
        if (!found_label)
            result.push_back(input[i++]);
    }

    return result;
}

std::vector<std::string> LabelProcessor::extractLabels(const std::string& input)
{
    static const std::regex label_pattern(R"(<[^>]*>)");
    
    std::vector<std::string> labels;
    std::sregex_iterator iter(input.begin(), input.end(), label_pattern);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        labels.push_back(iter->str());
    }

    return labels;
}

bool LabelProcessor::isKnownLabel(const std::string& label)
{
    const auto* def = registry_->findMatch(label);
    return def != nullptr;
}

bool LabelProcessor::isIgnoredLabel(const std::string& label)
{
    const auto* def = registry_->findMatch(label);
    if (!def) return false;
    
    // Ignored means it's known and will be removed
    return def->action == label_processing::LabelAction::Remove;
}

std::string LabelProcessor::processSelectSection(const std::string& content)
{
    std::istringstream iss(content);
    std::string line;
    std::ostringstream oss;
    bool first_line = true;

    while (std::getline(iss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (!line.empty()) {
            if (!first_line)
                oss << "\n";
            oss << "• " << line;
            first_line = false;
        }
    }

    return oss.str();
}
