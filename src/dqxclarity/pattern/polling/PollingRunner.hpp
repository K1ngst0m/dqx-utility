#pragma once

#include "PollingTask.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <memory>

namespace dqxclarity
{

struct PollingResult
{
    enum class Status
    {
        Matched,
        Timeout,
        Canceled,
        Error
    };

    Status status = Status::Canceled;
    std::string error_message;
    size_t ticks = 0;
    std::chrono::milliseconds elapsed{ 0 };
};

class PollingRunner
{
public:
    explicit PollingRunner(IMemoryScanner* scanner);

    PollingResult Run(IPollingTask& task, std::atomic<bool>& cancel_token) const;

private:
    IMemoryScanner* scanner_;
};

} // namespace dqxclarity
