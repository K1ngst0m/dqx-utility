#include "LabelProcessor.hpp"
#include "LabelPatterns.hpp"
#include "../processing/StageRunner.hpp"

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
    std::string result = input;

    // replace <br> with newline
    result = std::regex_replace(result, label_rules::br_pattern, "\n");

    // process select sections (<select_nc> ... <select_end>)
    std::smatch match;
    while (std::regex_search(result, match, label_rules::select_nc_pattern))
    {
        std::string processed = processSelectSection(match[1].str());
        result.replace(match.position(), match.length(), processed);
    }

    // process select_se_off sections
    while (std::regex_search(result, match, label_rules::select_se_off_pattern))
    {
        std::string processed = processSelectSection(match[1].str());
        result.replace(match.position(), match.length(), processed);
    }

    return result;
}

std::string LabelProcessor::processIgnoredLabels(const std::string& input)
{
    std::string result = input;

    // remove speed tags and attribute blocks via precompiled patterns
    result = std::regex_replace(result, label_rules::speed_pattern, "");
    result = std::regex_replace(result, label_rules::attr_pattern, "");

    // remove simple ignored literal labels (fast string operations)
    static const std::vector<std::string> ignored_literals = {
        "<close>", "<break>", "<bw_break>", "<end>", "<icon_exc>", "<left>"
    };

    for (const auto& lit : ignored_literals)
    {
        size_t pos = 0;
        while ((pos = result.find(lit, pos)) != std::string::npos)
        {
            result.erase(pos, lit.size());
        }
    }

    return result;
}

std::string LabelProcessor::trackUnknownLabels(const std::string& input)
{
    std::string result = input;
    std::vector<std::string> labels = extractLabels(result);

    for (const auto& label : labels)
    {
        if (!isKnownLabel(label) && !isIgnoredLabel(label))
        {
            // record unknown label
            unknown_labels_.insert(label);

            // remove all literal occurrences of the label (avoid regex overhead)
            size_t pos = 0;
            while ((pos = result.find(label, pos)) != std::string::npos)
            {
                result.erase(pos, label.size());
            }
        }
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
