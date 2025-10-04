#include "DQXClarityLauncher.hpp"
#include "ProcessDetector.hpp"

#include <plog/Log.h>

#include "dqxclarity/api/dqxclarity.hpp"

// Private implementation details
struct DQXClarityLauncher::Impl
{
    dqxclarity::Engine engine;
};

DQXClarityLauncher::DQXClarityLauncher()
    : pimpl_(std::make_unique<Impl>())
{
    dqxclarity::Config cfg{}; // defaults
    dqxclarity::Logger log{};
    log.info = [](const std::string& m){ PLOG_INFO << m; };
    log.warn = [](const std::string& m){ PLOG_WARNING << m; };
    log.error = [](const std::string& m){ PLOG_ERROR << m; };
    pimpl_->engine.initialize(cfg, std::move(log));
}

DQXClarityLauncher::~DQXClarityLauncher() = default;

bool DQXClarityLauncher::isDQXGameRunning() const
{
    return ProcessDetector::isProcessRunning("DQXGame.exe");
}

bool DQXClarityLauncher::launch()
{
    if (!isDQXGameRunning())
    {
        PLOG_WARNING << "Cannot start hook: DQXGame.exe is not running";
        return false;
    }
    return pimpl_->engine.start_hook();
}

bool DQXClarityLauncher::stop()
{
    return pimpl_->engine.stop_hook();
}

DQXClarityStatus DQXClarityLauncher::getStatus() const
{
    using S = dqxclarity::Status;
    switch (pimpl_->engine.status())
    {
        case S::Stopped: return DQXClarityStatus::Stopped;
        case S::Starting: return DQXClarityStatus::Running;
        case S::Hooked: return DQXClarityStatus::Running;
        case S::Stopping: return DQXClarityStatus::Running;
        case S::Error: return DQXClarityStatus::Stopped;
    }
    return DQXClarityStatus::Stopped;
}

std::string DQXClarityLauncher::getStatusString() const
{
    switch (getStatus())
    {
        case DQXClarityStatus::Stopped: return "Stopped";
        case DQXClarityStatus::Running: return "Running";
        case DQXClarityStatus::Connected: return "OK";
        case DQXClarityStatus::Disconnected: return "Disconnected";
        default: return "Unknown";
    }
}

