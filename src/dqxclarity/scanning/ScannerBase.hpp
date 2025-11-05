#pragma once

#include "IScanner.hpp"
#include "ScannerCreateInfo.hpp"
#include "../memory/IProcessMemory.hpp"
#include "../pattern/MemoryRegion.hpp"
#include "../pattern/Pattern.hpp"
#include "../api/dqxclarity.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace dqxclarity
{

/**
 * @brief Abstract base class for scanners with shared pattern matching logic
 * 
 * Provides common functionality for non-intrusive memory scanning:
 * - Pattern search in memory regions
 * - Memory region enumeration
 * - String extraction utilities
 * - Cached pattern location for performance
 */
class ScannerBase : public IScanner
{
public:
    explicit ScannerBase(const ScannerCreateInfo& create_info);
    virtual ~ScannerBase() = default;

    bool Initialize() override;
    bool Poll() override;
    bool IsActive() const override { return initialized_ && !shutdown_; }
    void Shutdown() override;

protected:
    static constexpr size_t kMaxStringLength = 4096;

    /**
     * @brief Override to perform initialization-specific logic
     * @return true on success
     */
    virtual bool OnInitialize() = 0;

    /**
     * @brief Override to perform polling-specific logic
     * @return true if new data was captured
     */
    virtual bool OnPoll() = 0;

    /**
     * @brief Override to perform shutdown-specific logic
     */
    virtual void OnShutdown() {}

    /**
     * @brief Find pattern in memory with caching for performance
     * @param pattern Pattern to search for
     * @param require_executable If true, search executable regions; if false, search non-executable
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t FindPattern(const Pattern& pattern, bool require_executable = false);

    /**
     * @brief Scan a specific memory region for a pattern
     * @param base_address Base address of the region
     * @param size Size of the region in bytes
     * @param pattern Pattern to search for
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t ScanRegionForPattern(uintptr_t base_address, size_t size, const Pattern& pattern);

    /**
     * @brief Scan all memory regions for a pattern
     * @param pattern Pattern to search for
     * @param require_executable If true, search executable regions; if false, search non-executable
     * @return Pattern address if found, 0 otherwise
     */
    uintptr_t ScanAllMemory(const Pattern& pattern, bool require_executable = false);

    /**
     * @brief Get non-executable memory regions for scanning
     * @return Vector of memory regions
     */
    std::vector<MemoryRegion> GetNonExecutableRegions();

    /**
     * @brief Get executable memory regions for scanning
     * @return Vector of memory regions
     */
    std::vector<MemoryRegion> GetExecutableRegions();

    /**
     * @brief Read a null-terminated string from memory
     * @param address Address to read from
     * @param output Output string
     * @param max_length Maximum string length to read
     * @return true if successful
     */
    bool ReadString(uintptr_t address, std::string& output, size_t max_length = kMaxStringLength);

    /**
     * @brief Find pattern in a buffer
     * @param buffer Buffer to search
     * @param buffer_size Size of buffer
     * @param pattern Pattern to search for
     * @return Offset of match, or SIZE_MAX if not found
     */
    size_t FindPatternInBuffer(const uint8_t* buffer, size_t buffer_size, const Pattern& pattern);

    IProcessMemory* memory_;
    Logger logger_;
    bool verbose_;
    Pattern pattern_;

    bool initialized_ = false;
    bool shutdown_ = false;

    uintptr_t last_pattern_addr_ = 0;
    uintptr_t last_region_base_ = 0;
    size_t last_region_size_ = 0;
};

} // namespace dqxclarity

