#pragma once

#include "../IMemoryScanner.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace dqxclarity
{

struct TickContext
{
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point now;
    size_t tick_count = 0;
};

enum class TerminationMode
{
    FirstMatch,
    Continuous
};

struct TaskDecision
{
    enum class Status
    {
        Continue,
        Match,
        Error
    };

    Status status = Status::Continue;
    std::string error_message;

    static TaskDecision Continue()
    {
        return TaskDecision{ Status::Continue, {} };
    }

    static TaskDecision Match()
    {
        return TaskDecision{ Status::Match, {} };
    }

    static TaskDecision Error(std::string message)
    {
        return TaskDecision{ Status::Error, std::move(message) };
    }
};

class IPollingTask
{
public:
    virtual ~IPollingTask() = default;

    virtual std::string_view Name() const = 0;
    virtual std::chrono::milliseconds PollInterval() const = 0;
    virtual std::optional<std::chrono::milliseconds> Timeout() const = 0;
    virtual TerminationMode Mode() const = 0;
    virtual TaskDecision Evaluate(IMemoryScanner& scanner, const TickContext& ctx) = 0;
};

} // namespace dqxclarity
