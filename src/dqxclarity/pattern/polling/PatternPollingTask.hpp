#pragma once

#include "PollingTask.hpp"
#include "../Pattern.hpp"
#include <functional>

namespace dqxclarity
{

class PatternPollingTask : public IPollingTask
{
public:
    using MatchCallback = std::function<void(uintptr_t)>;

    PatternPollingTask(std::string name, const Pattern& pattern, bool require_executable,
                       std::chrono::milliseconds interval, std::optional<std::chrono::milliseconds> timeout,
                       TerminationMode mode, MatchCallback on_match = nullptr);

    std::string_view Name() const override { return name_; }

    std::chrono::milliseconds PollInterval() const override { return interval_; }

    std::optional<std::chrono::milliseconds> Timeout() const override { return timeout_; }

    TerminationMode Mode() const override { return mode_; }

    TaskDecision Evaluate(IMemoryScanner& scanner, const TickContext& ctx) override;

private:
    std::string name_;
    const Pattern& pattern_;
    bool require_executable_ = true;
    std::chrono::milliseconds interval_;
    std::optional<std::chrono::milliseconds> timeout_;
    TerminationMode mode_;
    MatchCallback on_match_;
};

} // namespace dqxclarity
