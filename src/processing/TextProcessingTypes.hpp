#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace text_processing {

// Core data contracts for the text processing pipeline.
// All stages use these types as input/output to ensure clean interfaces.

// Raw dialog captured from game memory
struct RawDialog {
    std::string text;                         // Raw text with embedded game labels
    std::optional<std::string> speaker;       // NPC name (may be empty for some dialogs)
    std::uint64_t sequence_id = 0;            // Unique identifier for ordering
    std::chrono::system_clock::time_point captured_at = std::chrono::system_clock::now();
};

// After initial label processing (known labels handled, text normalized)
struct PreprocessedText {
    std::string normalized_text;              // Text with known labels processed/removed
    std::vector<std::string> stripped_labels; // Labels that were removed during processing
    bool has_selection_menu = false;          // Whether text contains selection options
    std::chrono::microseconds processing_time;
};

// Translation request payload (ready for backend submission)
struct TranslationRequest {
    std::string translatable_text;            // Text ready for translation (quotes masked, clean)
    std::string source_lang;                  // Usually "auto" for game text
    std::string target_lang;                  // Target language code (en-us, zh-cn, zh-tw)
    int backend_id;                          // Which translation service to use
    std::chrono::system_clock::time_point requested_at = std::chrono::system_clock::now();
};

// Translation result from backend
struct TranslationResult {
    std::string translated_text;              // Final translated text (quotes unmasked, aligned)
    std::string original_text;                // Original text before translation
    std::optional<std::string> error_message; // Error details if translation failed
    std::chrono::microseconds translation_time;

    // Metadata for pipeline tracing
    std::string cache_key_used;               // Cache key if result came from cache
    bool was_cached = false;                  // Whether result came from cache
    std::uint64_t job_id = 0;                 // Internal job ID for tracking
};

// Cache entry for translation results
struct CacheEntry {
    std::string translated_text;
    std::chrono::system_clock::time_point stored_at;
    std::size_t access_count = 0;
};

// Translation backend configuration (extracted from existing config)
struct BackendConfig {
    std::string api_key;
    std::string base_url;
    std::string model;
    int backend_type; // Maps to TranslationConfig::TranslationBackend
};

// Pipeline execution result wrapper (common for all stages)
template<typename T>
struct StageResult {
    T result;                                 // The actual result payload
    bool succeeded = true;                   // Whether the stage completed successfully
    std::optional<std::string> error;        // Error message if stage failed
    std::chrono::microseconds duration;      // How long the stage took to execute
    std::string stage_name;                  // Name of the stage (for logging/metrics)

    // Factory methods for cleaner usage
    static StageResult success(T r, std::chrono::microseconds time, const std::string& name) {
        StageResult res;
        res.result = std::move(r);
        res.succeeded = true;
        res.duration = time;
        res.stage_name = name;
        return res;
    }

    static StageResult failure(const std::string& err, std::chrono::microseconds time, const std::string& name) {
        StageResult res;
        res.succeeded = false;
        res.error = err;
        res.duration = time;
        res.stage_name = name;
        return res;
    }
};

} // namespace text_processing
