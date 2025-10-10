#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cctype>

#include "state/TranslationConfig.hpp"
#include "translate/ITranslator.hpp"
#include "translate/TranslationRequestBuilder.hpp"

class TranslateSession {
public:
    enum class SubmitKind { Cached, Queued, DroppedNotReady };
    struct SubmitResult { SubmitKind kind; std::uint64_t job_id = 0; std::string text; };
    struct CompletedEvent { std::uint64_t job_id = 0; std::string text; bool failed = false; std::string original_text; std::string error_message; };

    void setCapacity(std::size_t cap) { capacity_ = cap; }
    void enableCache(bool v) { cache_enabled_ = v; }
    void clear() { cache_.clear(); job_.clear(); cache_hits_ = 0; cache_misses_ = 0; }

    // Stats accessors
    std::uint64_t cacheHits() const { return cache_hits_; }
    std::uint64_t cacheMisses() const { return cache_misses_; }
    std::size_t cacheEntries() const { return cache_.size(); }
    std::size_t cacheCapacity() const { return capacity_; }
    bool isCacheEnabled() const { return cache_enabled_; }

    SubmitResult submit(const std::string& processed_text,
                        TranslationConfig::TranslationBackend backend,
                        TranslationConfig::TargetLang target,
                        translate::ITranslator* translator)
    {
        const std::string target_code = toTargetCode(target);

        // Compose a cache key based on the full processed text (including original quotes)
        std::string key;
        key.reserve(processed_text.size() + 32);
        key += "B:"; key += std::to_string(static_cast<int>(backend));
        key += "|T:"; key += target_code; key += "|"; key += processed_text;

        if (cache_enabled_) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                ++cache_hits_;
                return SubmitResult{SubmitKind::Cached, 0, it->second};
            }
            ++cache_misses_;
        }

        if (cache_.size() >= capacity_) cache_.clear();

        // Build a translation request (centralized masking/escaping)
        auto req = translate::build_translation_request(processed_text, "auto", target_code, static_cast<int>(backend));

        if (!translator || !translator->isReady()) {
            return SubmitResult{SubmitKind::DroppedNotReady, 0, {}};
        }

        std::uint64_t jid = 0;
        bool ok = translator->translate(req.translatable_text, req.source_lang, req.target_lang, jid);
        if (!ok || jid == 0) {
            return SubmitResult{SubmitKind::DroppedNotReady, 0, {}};
        }
        job_[jid] = JobInfo{std::move(key)};
        return SubmitResult{SubmitKind::Queued, jid, {}};
    }

    void onCompleted(const std::vector<translate::Completed>& results,
                     std::vector<CompletedEvent>& out_events)
    {
        out_events.clear();
        out_events.reserve(results.size());
        for (const auto& r : results) {
            auto it = job_.find(r.id);
            if (it != job_.end()) {
                const JobInfo& ji = it->second;
                if (r.failed) {
                    // Unmask original text
                    std::string orig = r.original_text;
                    static constexpr const char* kOpenQuote  = "\xE3\x80\x8C"; // U+300C
                    static constexpr const char* kCloseQuote = "\xE3\x80\x8D"; // U+300D
                    static constexpr const char* kTagOpen    = "<dqxlq/>";
                    static constexpr const char* kTagClose   = "<dqxrq/>";
                    replaceAllInPlace(orig, kTagOpen,  kOpenQuote);
                    replaceAllInPlace(orig, kTagClose, kCloseQuote);
                    out_events.push_back(CompletedEvent{r.id, {}, true, std::move(orig), r.error_message});
                } else {
                    // Unmask tags back to original corner quotes
                    std::string final = r.text;
                    static constexpr const char* kOpenQuote  = "\xE3\x80\x8C"; // U+300C
                    static constexpr const char* kCloseQuote = "\xE3\x80\x8D"; // U+300D
                    static constexpr const char* kTagOpen    = "<dqxlq/>";
                    static constexpr const char* kTagClose   = "<dqxrq/>";
                    replaceAllInPlace(final, kTagOpen,  kOpenQuote);
                    replaceAllInPlace(final, kTagClose, kCloseQuote);
                    alignAfterOpenQuote(final);
                    cache_[ji.key] = final;
                    out_events.push_back(CompletedEvent{r.id, std::move(final), false, {}, {}});
                }
                job_.erase(it);
            } else {
                out_events.push_back(CompletedEvent{r.id, r.text, r.failed, r.original_text, r.error_message});
            }
        }
    }

private:
    struct JobInfo {
        std::string key;
    };

    static std::string toTargetCode(TranslationConfig::TargetLang t)
    {
        switch (t) {
        case TranslationConfig::TargetLang::EN_US: return "en-us";
        case TranslationConfig::TargetLang::ZH_CN: return "zh-cn";
        case TranslationConfig::TargetLang::ZH_TW: return "zh-tw";
        }
        return "en-us";
    }

    // Align lines following an opening corner quote 「 by ensuring they start with a full-width space (U+3000)
    static void alignAfterOpenQuote(std::string& s)
    {
        static constexpr const char* kOpenQuote = "\xE3\x80\x8C"; // U+300C
        static constexpr const char* kFWSpace   = "\xE3\x80\x80"; // U+3000
        const std::size_t LQ = std::strlen(kOpenQuote);
        const std::size_t FWS = std::strlen(kFWSpace);

        std::string out;
        out.reserve(s.size() + 8);
        bool in_block = false;
        std::size_t pos = 0;
        while (pos <= s.size())
        {
            std::size_t nl = s.find('\n', pos);
            bool has_nl = (nl != std::string::npos);
            std::size_t end = has_nl ? nl : s.size();
            std::string line = s.substr(pos, end - pos);

            // Determine if line is empty (only ASCII spaces/tabs/CR)
            bool is_empty = true;
            for (unsigned char c : line) {
                if (!(c == ' ' || c == '\t' || c == '\r')) { is_empty = false; break; }
            }

            if (line.size() >= LQ && line.compare(0, LQ, kOpenQuote) == 0)
            {
                // First line of a quoted block
                in_block = true;
            }
            else if (in_block)
            {
                if (is_empty)
                {
                    in_block = false;
                }
                else if (line.size() >= LQ && line.compare(0, LQ, kOpenQuote) == 0)
                {
                    // New block begins
                    in_block = true;
                }
                else
                {
                    // Ensure a full-width space at start if not already present
                    if (!(line.size() >= FWS && line.compare(0, FWS, kFWSpace) == 0))
                    {
                        line.insert(0, kFWSpace);
                    }
                }
            }

            out += line;
            if (has_nl) out.push_back('\n');
            if (!has_nl) break;
            pos = nl + 1;
        }
        s.swap(out);
    }

    // Replace all occurrences of 'from' with 'to' in-place; returns count of replacements
    static std::size_t replaceAllInPlace(std::string& s, const char* from, const char* to)
    {
        if (!from || !*from) return 0;
        const std::string needle(from);
        const std::string repl = to ? std::string(to) : std::string();
        std::size_t pos = 0, count = 0;
        while ((pos = s.find(needle, pos)) != std::string::npos) {
            s.replace(pos, needle.size(), repl);
            pos += repl.size();
            ++count;
        }
        return count;
    }

    std::unordered_map<std::string, std::string> cache_;
    std::unordered_map<std::uint64_t, JobInfo> job_;
    std::size_t capacity_ = 5000;
    bool cache_enabled_ = true;
    std::uint64_t cache_hits_ = 0;
    std::uint64_t cache_misses_ = 0;
};
