#include "PollingRunner.hpp"
#include <thread>

namespace dqxclarity
{

PollingRunner::PollingRunner(std::shared_ptr<IMemoryScanner> scanner)
    : scanner_(std::move(scanner))
{
}

PollingResult PollingRunner::Run(IPollingTask& task, std::atomic<bool>& cancel_token) const
{
    PollingResult result;
    const auto start = std::chrono::steady_clock::now();
    auto next_tick = start;
    TickContext ctx{ start, start, 0 };

    while (true)
    {
        if (cancel_token.load())
        {
            result.status = PollingResult::Status::Canceled;
            break;
        }

        const auto timeout = task.Timeout();
        ctx.now = std::chrono::steady_clock::now();
        if (timeout && ctx.now - start >= *timeout)
        {
            result.status = PollingResult::Status::Timeout;
            break;
        }

        if (ctx.now < next_tick)
        {
            std::this_thread::sleep_for(next_tick - ctx.now);
            ctx.now = std::chrono::steady_clock::now();
        }

        if (!scanner_)
        {
            result.status = PollingResult::Status::Error;
            result.error_message = "PollingRunner: scanner unavailable";
            break;
        }

        auto decision = task.Evaluate(*scanner_, ctx);
        ++ctx.tick_count;
        next_tick = start + (ctx.tick_count * task.PollInterval());

        if (decision.status == TaskDecision::Status::Error)
        {
            result.status = PollingResult::Status::Error;
            result.error_message = decision.error_message;
            break;
        }

        if (decision.status == TaskDecision::Status::Match && task.Mode() == TerminationMode::FirstMatch)
        {
            result.status = PollingResult::Status::Matched;
            break;
        }
    }

    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    result.ticks = ctx.tick_count;
    return result;
}

} // namespace dqxclarity
