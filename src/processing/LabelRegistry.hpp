#pragma once

#include <functional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace label_processing
{

// Label processing action types
enum class LabelAction
{
    Transform, // Transform content (e.g., <br> -> \n)
    Remove, // Remove the label entirely
    ProcessPaired // Process paired label content (e.g., <select>...</select_end>)
};

// Label matching type
enum class LabelMatchType
{
    Literal, // Exact match: <br>
    Wildcard, // Pattern match: <select *>, <speed=*>
    Paired // Paired tags: <select>...<select_end>
};

// Function signature for label content processors
using ContentProcessor = std::function<std::string(const std::string&)>;

// Label definition structure
struct LabelDefinition
{
    std::string signature; // e.g., "<br>", "<select *>", "<speed=*>"
    LabelMatchType match_type;
    LabelAction action;
    std::string replacement; // For Transform action
    std::string pair_close; // For Paired labels (e.g., "<select_end>")
    ContentProcessor processor; // Custom processing function
    std::regex compiled_pattern; // Pre-compiled regex
    bool case_sensitive;

    LabelDefinition()
        : match_type(LabelMatchType::Literal)
        , action(LabelAction::Remove)
        , case_sensitive(false)
        , processor(nullptr)
    {
    }
};

// Label registry manages all known label definitions
class LabelRegistry
{
public:
    LabelRegistry();

    // Check if a label matches any registered definition
    const LabelDefinition* findMatch(const std::string& label) const;

    // Process a label according to its definition
    std::string processLabel(const std::string& label, const LabelDefinition* def) const;

    // Get all pair-close patterns for tracking
    std::vector<std::string> getPairClosePatterns() const;

private:
    void registerLabel(LabelDefinition def);
    void initializeDefaultLabels();

    std::vector<LabelDefinition> definitions_;
    std::unordered_map<std::string, size_t> literal_index_; // Fast lookup for literals
};

} // namespace label_processing