#include "LabelProcessor.hpp"
#include "LabelPatterns.hpp"
#include "StageRunner.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

LabelProcessor::LabelProcessor(UnknownLabelRepository* repo)
    : repository_(repo)
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
    // Stage 1: known label processing
    auto known_stage = processing::run_stage<std::string>("label_known", [&]() {
        return this->processKnownLabels(input);
    });
    if (!known_stage.succeeded) {
        return input; // fallback to original on failure
    }

    // Stage 2: ignored label removal
    auto ignored_stage = processing::run_stage<std::string>("label_ignored", [&]() {
        return this->processIgnoredLabels(known_stage.result);
    });
    if (!ignored_stage.succeeded) {
        return known_stage.result;
    }

    // Stage 3: unknown label tracking & removal
    auto unknown_stage = processing::run_stage<std::string>("label_unknowns", [&]() {
        return this->trackUnknownLabels(ignored_stage.result);
    });
    if (!unknown_stage.succeeded) {
        return ignored_stage.result;
    }

    return unknown_stage.result;
}

std::string LabelProcessor::processKnownLabels(const std::string& input)
{
    std::string result = std::regex_replace(input, label_rules::br_pattern, "\n");
    
    auto process_select_pattern = [this](std::string& text, const std::regex& pattern) {
        std::string output;
        std::sregex_iterator iter(text.begin(), text.end(), pattern);
        std::sregex_iterator end;
        
        size_t last_pos = 0;
        for (; iter != end; ++iter) {
            output.append(text, last_pos, iter->position() - last_pos);
            output.append(processSelectSection((*iter)[1].str()));
            last_pos = iter->position() + iter->length();
        }
        output.append(text, last_pos, std::string::npos);
        return output;
    };
    
    result = process_select_pattern(result, label_rules::select_nc_pattern);
    result = process_select_pattern(result, label_rules::select_se_off_pattern);
    
    return result;
}

std::string LabelProcessor::processIgnoredLabels(const std::string& input)
{
    std::string result = std::regex_replace(input, label_rules::speed_pattern, "");
    result = std::regex_replace(result, label_rules::attr_pattern, "");

    static const std::vector<std::string> ignored_literals = {
        "<close>", "<break>", "<bw_break>", "<end>", "<icon_exc>", "<left>"
    };

    for (const auto& lit : ignored_literals)
    {
        size_t pos = 0;
        while ((pos = result.find(lit, pos)) != std::string::npos)
            result.erase(pos, lit.size());
    }

    return result;
}

std::string LabelProcessor::trackUnknownLabels(const std::string& input)
{
    std::vector<std::string> labels = extractLabels(input);
    std::unordered_set<std::string> unknown_to_remove;

    for (const auto& label : labels)
    {
        if (!isKnownLabel(label) && !isIgnoredLabel(label))
        {
            unknown_labels_.insert(label);
            unknown_to_remove.insert(label);
        }
    }

    if (unknown_to_remove.empty())
        return input;

    std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); )
    {
        bool found_label = false;
        for (const auto& label : unknown_to_remove)
        {
            if (input.compare(i, label.size(), label) == 0)
            {
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
    std::vector<std::string> labels;
    std::sregex_iterator iter(input.begin(), input.end(), label_rules::label_pattern);
    std::sregex_iterator end;

    for (; iter != end; ++iter)
    {
        labels.push_back(iter->str());
    }

    return labels;
}

bool LabelProcessor::isKnownLabel(const std::string& label)
{
    static const std::unordered_set<std::string> known_labels = {
        "<br>", "<select_nc>", "<select_end>", "<select_se_off>"
    };

    return known_labels.count(label) > 0;
}

bool LabelProcessor::isIgnoredLabel(const std::string& label)
{
    // speed pattern handled via regex, attribute blocks via attr_pattern
    if (std::regex_match(label, label_rules::speed_pattern))
        return true;

    static const std::unordered_set<std::string> ignored_labels = {
        "<close>", "<break>", "<bw_break>", "<end>", "<icon_exc>", "<left>", "<attr>", "<end_attr>"
    };

    return ignored_labels.count(label) > 0;
}

std::string LabelProcessor::processSelectSection(const std::string& content)
{
    std::istringstream iss(content);
    std::string line;
    std::ostringstream oss;
    bool first_line = true;

    while (std::getline(iss, line))
    {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (!line.empty())
        {
            if (!first_line)
                oss << "\n";
            oss << "• " << line;
            first_line = false;
        }
    }

    return oss.str();
}
