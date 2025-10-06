#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "state/TranslationConfig.hpp"
#include "translate/ITranslator.hpp"

class TranslateSession {
public:
    enum class SubmitKind { Cached, Queued, DroppedNotReady };
    struct SubmitResult { SubmitKind kind; std::uint64_t job_id = 0; std::string text; };
    struct CompletedEvent { std::uint64_t job_id = 0; std::string text; };

    void setCapacity(std::size_t cap) { capacity_ = cap; }
    void enableCache(bool v) { cache_enabled_ = v; }
    void clear() { cache_.clear(); job_key_.clear(); }

    SubmitResult submit(const std::string& processed_text,
                        TranslationConfig::TranslationBackend backend,
                        TranslationConfig::TargetLang target,
                        translate::ITranslator* translator)
    {
        const std::string target_code = toTargetCode(target);
        std::string key;
        key.reserve(processed_text.size() + 32);
        key += "B:"; key += std::to_string(static_cast<int>(backend));
        key += "|T:"; key += target_code; key += "|"; key += processed_text;

        if (cache_enabled_) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return SubmitResult{SubmitKind::Cached, 0, it->second};
            }
        }

        if (cache_.size() >= capacity_) cache_.clear();

        if (!translator || !translator->isReady()) {
            return SubmitResult{SubmitKind::DroppedNotReady, 0, {}};
        }

        std::uint64_t jid = 0;
        bool ok = translator->translate(processed_text, "auto", target_code, jid);
        if (!ok || jid == 0) {
            return SubmitResult{SubmitKind::DroppedNotReady, 0, {}};
        }
        job_key_[jid] = std::move(key);
        return SubmitResult{SubmitKind::Queued, jid, {}};
    }

    void onCompleted(const std::vector<translate::Completed>& results,
                     std::vector<CompletedEvent>& out_events)
    {
        out_events.clear();
        out_events.reserve(results.size());
        for (const auto& r : results) {
            auto it = job_key_.find(r.id);
            if (it != job_key_.end()) {
                cache_[it->second] = r.text;
                out_events.push_back(CompletedEvent{r.id, r.text});
                job_key_.erase(it);
            } else {
                out_events.push_back(CompletedEvent{r.id, r.text});
            }
        }
    }

private:
    static std::string toTargetCode(TranslationConfig::TargetLang t)
    {
        switch (t) {
        case TranslationConfig::TargetLang::EN_US: return "en-us";
        case TranslationConfig::TargetLang::ZH_CN: return "zh-cn";
        case TranslationConfig::TargetLang::ZH_TW: return "zh-tw";
        }
        return "en-us";
    }

    std::unordered_map<std::string, std::string> cache_;
    std::unordered_map<std::uint64_t, std::string> job_key_;
    std::size_t capacity_ = 5000;
    bool cache_enabled_ = true;
};