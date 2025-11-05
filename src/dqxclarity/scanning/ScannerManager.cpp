#include "ScannerManager.hpp"
#include "IScanner.hpp"

#include <iostream>

namespace dqxclarity
{

bool ScannerManager::RegisterScanner(ScannerType type, std::unique_ptr<IScanner> scanner)
{
    if (!scanner)
        return false;

    scanners_[type] = std::move(scanner);
    return true;
}

void ScannerManager::RemoveAllScanners()
{
    StopAllScanners();
    scanners_.clear();
}

IScanner* ScannerManager::GetScanner(ScannerType type)
{
    auto it = scanners_.find(type);
    if (it != scanners_.end())
        return it->second.get();
    return nullptr;
}

bool ScannerManager::StartContinuousScanners()
{
    bool all_started = true;

    for (const auto type : {ScannerType::Dialog, ScannerType::NoticeScreen, ScannerType::PostLogin})
    {
        auto* scanner = GetScanner(type);
        if (scanner && !scanner->IsActive())
        {
            if (!scanner->Initialize())
            {
                if (logger_.warn)
                    logger_.warn("Failed to initialize scanner: " + std::string(GetScannerTypeName(type)));
                all_started = false;
            }
        }
    }

    return all_started;
}

void ScannerManager::StopAllScanners()
{
    for (auto& [type, scanner] : scanners_)
    {
        if (scanner && scanner->IsActive())
        {
            scanner->Shutdown();
        }
    }
}

void ScannerManager::PollAllScanners()
{
    for (const auto type : {ScannerType::Dialog, ScannerType::NoticeScreen, ScannerType::PostLogin})
    {
        auto* scanner = GetScanner(type);
        if (scanner && scanner->IsActive())
        {
            scanner->Poll();
        }
    }
}

const char* ScannerManager::GetScannerTypeName(ScannerType type)
{
    switch (type)
    {
        case ScannerType::Dialog:
            return "Dialog";
        case ScannerType::NoticeScreen:
            return "NoticeScreen";
        case ScannerType::PostLogin:
            return "PostLogin";
        case ScannerType::PlayerName:
            return "PlayerName";
        default:
            return "Unknown";
    }
}

} // namespace dqxclarity

