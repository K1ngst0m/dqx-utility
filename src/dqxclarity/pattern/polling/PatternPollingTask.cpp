#include <utility>
#include "PatternPollingTask.hpp"

namespace dqxclarity
{

PatternPollingTask::PatternPollingTask(std::string name, const Pattern& pattern, bool require_executable,
                                       std::chrono::milliseconds interval,
                                       std::optional<std::chrono::milliseconds> timeout, TerminationMode mode,
                                       MatchCallback on_match)
    : name_(std::move(name))
    , pattern_(pattern)
    , require_executable_(require_executable)
    , interval_(interval)
    , timeout_(timeout)
    , mode_(mode)
    , on_match_(std::move(on_match))
{
}

TaskDecision PatternPollingTask::Evaluate(IMemoryScanner& scanner, const TickContext&)
{
    auto address = scanner.ScanProcess(pattern_, require_executable_);
    if (address)
    {
        if (on_match_)
        {
            on_match_(*address);
        }
        return TaskDecision::Match();
    }
    return TaskDecision::Continue();
}

} // namespace dqxclarity
