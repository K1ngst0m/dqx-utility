#pragma once

#include "ScannerCreateInfo.hpp"
#include "../api/dqxclarity.hpp"
#include "../memory/IProcessMemory.hpp"

#include <map>
#include <memory>

namespace dqxclarity
{

class IScanner;

enum class ScannerType
{
    Dialog,
    NoticeScreen,
    PostLogin,
    PlayerName
};

/**
 * @brief Centralized lifecycle manager for all memory scanners
 * 
 * Manages scanner creation, polling, and state tracking.
 * Parallel to HookManager for non-intrusive operations.
 */
class ScannerManager
{
public:
    ScannerManager() = default;
    ~ScannerManager() = default;

    ScannerManager(const ScannerManager&) = delete;
    ScannerManager& operator=(const ScannerManager&) = delete;

    /**
     * @brief Register a scanner with the manager
     * 
     * @param type Scanner type
     * @param scanner Unique pointer to scanner instance
     * @return true if registered successfully
     */
    bool RegisterScanner(ScannerType type, std::unique_ptr<IScanner> scanner);

    /**
     * @brief Remove all scanners and shutdown
     */
    void RemoveAllScanners();

    /**
     * @brief Access a scanner by type
     * 
     * @param type Scanner type to retrieve
     * @return Pointer to scanner instance, or nullptr if not registered
     */
    IScanner* GetScanner(ScannerType type);

    /**
     * @brief Start continuous scanners (NoticeScreen, PostLogin, Dialog)
     * 
     * Initializes scanners that run continuously in the background.
     * @return true if all continuous scanners started successfully
     */
    bool StartContinuousScanners();

    /**
     * @brief Stop all scanners
     */
    void StopAllScanners();

    /**
     * @brief Poll all continuous scanners
     * 
     * Should be called from Engine polling loop.
     */
    void PollAllScanners();

    /**
     * @brief Get scanner type name for logging
     * 
     * @param type Scanner type
     * @return String name of scanner type
     */
    static const char* GetScannerTypeName(ScannerType type);

private:
    std::map<ScannerType, std::unique_ptr<IScanner>> scanners_;
    IProcessMemory* memory_ = nullptr;
    Logger logger_{};
};

} // namespace dqxclarity

