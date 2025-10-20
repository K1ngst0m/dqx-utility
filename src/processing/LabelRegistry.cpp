#include "LabelRegistry.hpp"
#include <algorithm>
#include <sstream>

namespace label_processing {

namespace {

inline std::string escape_regex(const std::string& s)
{
    static const std::string special = R"(\.^$|()[]+?{}-)";
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s)
    {
        if (special.find(c) != std::string::npos)
        {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// Helper to create regex from wildcard pattern
std::regex createWildcardRegex(const std::string& pattern, bool case_sensitive = false) {
    std::string regex_str;
    regex_str.reserve(pattern.size() * 2);
    
    // Manually escape and convert wildcards
    static const std::string special = R"(\.^$|()[]+?{}-)";  // Note: * is NOT in this list
    
    for (char c : pattern) {
        if (c == '*') {
            // Wildcard - convert to regex pattern
            regex_str += ".*?";
        } else if (special.find(c) != std::string::npos) {
            // Special regex character - escape it
            regex_str += '\\';
            regex_str += c;
        } else {
            // Regular character
            regex_str += c;
        }
    }
    
    // Anchor to ensure full match
    std::string final_pattern = "^" + regex_str + "$";
    
    auto flags = std::regex_constants::ECMAScript;
    if (!case_sensitive) {
        flags |= std::regex_constants::icase;
    }
    
    return std::regex(final_pattern, flags);
}

// Process selection menu content - add bullets and format
std::string processSelectionContent(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    std::ostringstream oss;
    bool first_line = true;

    while (std::getline(iss, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (!line.empty()) {
            if (!first_line) {
                oss << "\n";
            }
            oss << "• " << line;
            first_line = false;
        }
    }

    return oss.str();
}

} // anonymous namespace

LabelRegistry::LabelRegistry() {
    initializeDefaultLabels();
}

void LabelRegistry::registerLabel(LabelDefinition def) {
    // Pre-compile regex for wildcard patterns
    if (def.match_type == LabelMatchType::Wildcard) {
        def.compiled_pattern = createWildcardRegex(def.signature, def.case_sensitive);
    } else if (def.match_type == LabelMatchType::Paired) {
        std::string opening = def.signature;
        if (opening.find('*') != std::string::npos) {
            def.compiled_pattern = createWildcardRegex(opening, def.case_sensitive);
        } else {
            std::string escaped = escape_regex(opening);
            std::string regex_str = "^" + escaped + "$";
            auto flags = std::regex_constants::ECMAScript;
            if (!def.case_sensitive) {
                flags |= std::regex_constants::icase;
            }
            def.compiled_pattern = std::regex(regex_str, flags);
        }
    }
    
    // Add to literal index for fast lookup if it's a simple literal
    if (def.match_type == LabelMatchType::Literal) {
        std::string key = def.signature;
        if (!def.case_sensitive) {
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        }
        literal_index_[key] = definitions_.size();
    }
    
    definitions_.push_back(std::move(def));
}

void LabelRegistry::initializeDefaultLabels() {
    // Transform labels
    {
        LabelDefinition br_def;
        br_def.signature = "<br>";
        br_def.match_type = LabelMatchType::Literal;
        br_def.action = LabelAction::Transform;
        br_def.replacement = "\n";
        br_def.case_sensitive = false;
        registerLabel(br_def);
    }
    
    // Paired selection labels - these process the content between tags
    {
        LabelDefinition select_def;
        select_def.signature = "<select>";
        select_def.match_type = LabelMatchType::Paired;
        select_def.action = LabelAction::ProcessPaired;
        select_def.pair_close = "<select_end>";
        select_def.processor = processSelectionContent;
        select_def.case_sensitive = false;
        registerLabel(select_def);
    }
    
    {
        LabelDefinition select_nc_def;
        select_nc_def.signature = "<select_nc>";
        select_nc_def.match_type = LabelMatchType::Paired;
        select_nc_def.action = LabelAction::ProcessPaired;
        select_nc_def.pair_close = "<select_end>";
        select_nc_def.processor = processSelectionContent;
        select_nc_def.case_sensitive = false;
        registerLabel(select_nc_def);
    }
    
    {
        LabelDefinition select_se_off_def;
        select_se_off_def.signature = "<select_se_off>";
        select_se_off_def.match_type = LabelMatchType::Paired;
        select_se_off_def.action = LabelAction::ProcessPaired;
        select_se_off_def.pair_close = "<select_end>";
        select_se_off_def.processor = processSelectionContent;
        select_se_off_def.case_sensitive = false;
        registerLabel(select_se_off_def);
    }
    
    // Wildcard paired selections (e.g., <select 1>, <select 2>)
    {
        LabelDefinition select_wild_def;
        select_wild_def.signature = "<select *>";
        select_wild_def.match_type = LabelMatchType::Paired;
        select_wild_def.action = LabelAction::ProcessPaired;
        select_wild_def.pair_close = "<select_end>";
        select_wild_def.processor = processSelectionContent;
        select_wild_def.case_sensitive = false;
        registerLabel(select_wild_def);
    }
    
    // Wildcard select_se_off with parameters (e.g., <select_se_off 2>)
    {
        LabelDefinition select_se_off_wild_def;
        select_se_off_wild_def.signature = "<select_se_off *>";
        select_se_off_wild_def.match_type = LabelMatchType::Paired;
        select_se_off_wild_def.action = LabelAction::ProcessPaired;
        select_se_off_wild_def.pair_close = "<select_end>";
        select_se_off_wild_def.processor = processSelectionContent;
        select_se_off_wild_def.case_sensitive = false;
        registerLabel(select_se_off_wild_def);
    }
    
    // Wildcard remove labels - these match patterns with variable content
    {
        LabelDefinition speed_def;
        speed_def.signature = "<speed=*>";
        speed_def.match_type = LabelMatchType::Wildcard;
        speed_def.action = LabelAction::Remove;
        speed_def.case_sensitive = false;
        registerLabel(speed_def);
    }
    
    {
        LabelDefinition case_def;
        case_def.signature = "<case *>";
        case_def.match_type = LabelMatchType::Wildcard;
        case_def.action = LabelAction::Remove;
        case_def.case_sensitive = false;
        registerLabel(case_def);
    }
    
    {
        LabelDefinition yesno_def;
        yesno_def.signature = "<yesno *>";
        yesno_def.match_type = LabelMatchType::Wildcard;
        yesno_def.action = LabelAction::Remove;
        yesno_def.case_sensitive = false;
        registerLabel(yesno_def);
    }
    
    {
        LabelDefinition se_nots_def;
        se_nots_def.signature = "<se_nots *>";
        se_nots_def.match_type = LabelMatchType::Wildcard;
        se_nots_def.action = LabelAction::Remove;
        se_nots_def.case_sensitive = false;
        registerLabel(se_nots_def);
    }
    
    // Attribute block - paired removal
    {
        LabelDefinition attr_def;
        attr_def.signature = "<attr>";
        attr_def.match_type = LabelMatchType::Paired;
        attr_def.action = LabelAction::Remove;
        attr_def.pair_close = "<end_attr>";
        attr_def.case_sensitive = false;
        registerLabel(attr_def);
    }
    
    // Simple removal labels - literal matches
    const std::vector<std::string> simple_removals = {
        "<close>",
        "<break>",
        "<bw_break>",
        "<end>",
        "<icon_exc>",
        "<left>",
        "<turn_pc>",
        "<turn_end>",
        "<case_cancel>",
        "<case_end>",
        "<pipipi_off>",
        "<select_end>"  // Standalone closing tag (shouldn't appear but handle it)
    };
    
    for (const auto& sig : simple_removals) {
        LabelDefinition def;
        def.signature = sig;
        def.match_type = LabelMatchType::Literal;
        def.action = LabelAction::Remove;
        def.case_sensitive = false;
        registerLabel(def);
    }
}

const LabelDefinition* LabelRegistry::findMatch(const std::string& label) const {
    // Fast path: literal lookup
    std::string lower_label = label;
    std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(), ::tolower);
    
    auto lit_it = literal_index_.find(lower_label);
    if (lit_it != literal_index_.end()) {
        return &definitions_[lit_it->second];
    }
    
    // Slow path: check wildcards and paired patterns
    for (const auto& def : definitions_) {
        if (def.match_type == LabelMatchType::Literal) {
            continue; // Already checked
        }
        
        if (def.match_type == LabelMatchType::Wildcard || def.match_type == LabelMatchType::Paired) {
            if (std::regex_match(label, def.compiled_pattern)) {
                return &def;
            }
        }
    }
    
    return nullptr;
}

std::string LabelRegistry::processLabel(const std::string& label, const LabelDefinition* def) const {
    if (!def) {
        return label; // Unknown label, return as-is
    }
    
    switch (def->action) {
        case LabelAction::Transform:
            return def->replacement;
        
        case LabelAction::Remove:
            return "";
        
        case LabelAction::ProcessPaired:
            // For paired labels, this is just the opening tag - content processing happens elsewhere
            return "";
    }
    
    return "";
}

std::vector<std::string> LabelRegistry::getPairClosePatterns() const {
    std::vector<std::string> patterns;
    for (const auto& def : definitions_) {
        if (def.match_type == LabelMatchType::Paired && !def.pair_close.empty()) {
            patterns.push_back(def.pair_close);
        }
    }
    return patterns;
}

} // namespace label_processing