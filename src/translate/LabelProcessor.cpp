#include "LabelProcessor.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

LabelProcessor::LabelProcessor()
{
    loadUnknownLabels();
}

LabelProcessor::~LabelProcessor()
{
    saveUnknownLabels();
}

std::string LabelProcessor::processText(const std::string& input)
{
    std::string result = input;
    result = processKnownLabels(result);
    result = processIgnoredLabels(result);
    result = trackUnknownLabels(result);
    return result;
}

std::string LabelProcessor::processKnownLabels(const std::string& input)
{
    std::string result = input;
    
    result = std::regex_replace(result, std::regex("<br>"), "\n");
    
    std::regex select_nc_pattern(R"(<select_nc>([\s\S]*?)<select_end>)", std::regex_constants::icase | std::regex_constants::ECMAScript);
    std::smatch match;
    while (std::regex_search(result, match, select_nc_pattern))
    {
        std::string processed = processSelectSection(match[1].str());
        result.replace(match.position(), match.length(), processed);
    }
    
    std::regex select_se_off_pattern(R"(<select_se_off>([\s\S]*?)<select_end>)", std::regex_constants::icase | std::regex_constants::ECMAScript);
    while (std::regex_search(result, match, select_se_off_pattern))
    {
        std::string processed = processSelectSection(match[1].str());
        result.replace(match.position(), match.length(), processed);
    }
    
    return result;
}

std::string LabelProcessor::processIgnoredLabels(const std::string& input)
{
    std::string result = input;
    
    result = std::regex_replace(result, std::regex(R"(<speed=[^>]*>)"), "");
    result = std::regex_replace(result, std::regex("<close>"), "");
    result = std::regex_replace(result, std::regex("<break>"), "");
    result = std::regex_replace(result, std::regex("<bw_break>"), "");
    result = std::regex_replace(result, std::regex("<end>"), "");
    result = std::regex_replace(result, std::regex("<icon_exc>"), "");
    result = std::regex_replace(result, std::regex("<left>"), "");
    
    result = std::regex_replace(result, std::regex(R"(<attr>.*?<end_attr>)"), "");
    
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
            unknown_labels_.insert(label);
            result = std::regex_replace(result, std::regex(std::regex_replace(label, std::regex(R"([\[\](){}.*+?^$|\\])"), R"(\$&)")), "");
        }
    }
    
    return result;
}

std::vector<std::string> LabelProcessor::extractLabels(const std::string& input)
{
    std::vector<std::string> labels;
    std::regex label_pattern(R"(<[^>]*>)");
    std::sregex_iterator iter(input.begin(), input.end(), label_pattern);
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
    static const std::regex speed_pattern(R"(<speed=[^>]*>)");
    if (std::regex_match(label, speed_pattern))
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

void LabelProcessor::loadUnknownLabels()
{
    std::ifstream file(kUnknownLabelsFile);
    if (!file.is_open())
        return;
    
    std::string label;
    while (std::getline(file, label))
    {
        if (!label.empty())
            unknown_labels_.insert(label);
    }
}

void LabelProcessor::saveUnknownLabels()
{
    if (unknown_labels_.empty())
        return;
    
    std::ofstream file(kUnknownLabelsFile);
    if (!file.is_open())
        return;
    
    for (const auto& label : unknown_labels_)
    {
        file << label << "\n";
    }
}