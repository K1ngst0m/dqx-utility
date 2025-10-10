#pragma once

#include "../processing/TextProcessingTypes.hpp"
#include <string>

namespace translate {

// Build a TranslationRequest from cleaned/transformed text and configuration.
// This function performs light masking (escape double quotes) to avoid backend parsing issues.
// source_lang is typically "auto"; target_lang is like "en-us", "zh-cn", etc.
// backend_id maps to existing translator backend identifiers in the codebase.
text_processing::TranslationRequest build_translation_request(const std::string& text,
                                                               const std::string& source_lang,
                                                               const std::string& target_lang,
                                                               int backend_id);

} // namespace translate
